/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_bt_policy.h"

#include <syslog.h>

#include "cras/src/server/cras_a2dp_endpoint.h"
#include "cras/src/server/cras_bt_constants.h"
#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_hfp_ag_profile.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_main_message.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_tm.h"
#include "third_party/utlist/utlist.h"

/* Check profile connections every 2 seconds and rerty 30 times maximum.
 * Attemp to connect profiles which haven't been ready every 3 retries_left.
 */
static const unsigned int CONN_WATCH_PERIOD_MS = 2000;
static const unsigned int CONN_WATCH_MAX_RETRIES = 30;

static const unsigned int PROFILE_SWITCH_DELAY_MS = 500;

enum BT_POLICY_COMMAND {
  BT_POLICY_SWITCH_PROFILE,
  BT_POLICY_SCHEDULE_SUSPEND,
  BT_POLICY_CANCEL_SUSPEND,
};

struct bt_policy_msg {
  struct cras_main_message header;
  enum BT_POLICY_COMMAND cmd;
  struct bt_io_manager* mgr;
  struct cras_bt_device* device;
  struct cras_iodev* dev;
  unsigned int arg1;
  unsigned int arg2;
};

struct profile_switch_policy {
  struct bt_io_manager* mgr;
  struct cras_timer* timer;
  struct profile_switch_policy *prev, *next;
};

struct profile_switch_policy* profile_switch_policies;

struct suspend_policy {
  struct cras_bt_device* device;
  // The reason code for why suspend is scheduled.
  enum cras_bt_policy_suspend_reason suspend_reason;
  struct cras_timer* timer;
  struct suspend_policy *prev, *next;
};

struct suspend_policy* suspend_policies;

struct connection_watch {
  struct cras_bt_device* device;
  int retries_left;
  struct cras_timer* timer;
  struct connection_watch *prev, *next;
};

struct connection_watch* conn_watch_policies;

static void profile_switch_delay_cb(struct cras_timer* timer, void* arg) {
  struct profile_switch_policy* policy = (struct profile_switch_policy*)arg;
  struct cras_iodev* iodev;

  /*
   * During the |PROFILE_SWITCH_DELAY_MS| time interval, BT iodev could
   * have been enabled by others, and its active profile may have changed.
   * If iodev has been enabled, that means it has already picked up a
   * reasonable profile to use and audio thread is accessing iodev now.
   * We should NOT call into update_active_node from main thread
   * because that may mess up the active node content.
   */
  iodev = policy->mgr->bt_iodevs[CRAS_STREAM_OUTPUT];
  if (iodev) {
    iodev->update_active_node(iodev, 0, 1);
    cras_iodev_list_resume_dev(iodev->info.idx);
  }

  DL_DELETE(profile_switch_policies, policy);
  free(policy);
}

static void switch_profile_with_delay(struct bt_io_manager* mgr) {
  struct cras_tm* tm = cras_system_state_get_tm();
  struct profile_switch_policy* policy;

  DL_SEARCH_SCALAR(profile_switch_policies, policy, mgr, mgr);
  if (policy) {
    cras_tm_cancel_timer(tm, policy->timer);
    policy->timer = NULL;
  } else {
    policy = (struct profile_switch_policy*)calloc(1, sizeof(*policy));
  }

  policy->mgr = mgr;
  policy->timer = cras_tm_create_timer(tm, PROFILE_SWITCH_DELAY_MS,
                                       profile_switch_delay_cb, policy);
  DL_APPEND(profile_switch_policies, policy);
}

static void switch_profile(struct bt_io_manager* mgr) {
  struct cras_iodev* iodev;
  int dir;

  /* If a bt iodev is active, temporarily force close it.
   * Note that we need to check all bt_iodevs for the situation that both
   * input and output are active while switches from HFP to A2DP.
   */
  for (dir = 0; dir < CRAS_NUM_DIRECTIONS; dir++) {
    iodev = mgr->bt_iodevs[dir];
    if (!iodev) {
      continue;
    }
    cras_iodev_list_suspend_dev(iodev->info.idx);
  }

  mgr->is_profile_switching = false;

  for (dir = 0; dir < CRAS_NUM_DIRECTIONS; dir++) {
    iodev = mgr->bt_iodevs[dir];
    if (!iodev) {
      continue;
    }

    /* If the iodev was active or this profile switching is
     * triggered at opening iodev, add it to active dev list.
     * However for the output iodev, adding it back to active dev
     * list could cause immediate switching from HFP to A2DP if
     * there exists an output stream. Certain headset/speaker
     * would fail to playback afterwards when the switching happens
     * too soon, so put this task in a delayed callback.
     */
    if (dir == CRAS_STREAM_INPUT) {
      iodev->update_active_node(iodev, 0, 1);
      cras_iodev_list_resume_dev(iodev->info.idx);
    } else {
      switch_profile_with_delay(mgr);
    }
  }
}

static void init_bt_policy_msg(struct bt_policy_msg* msg,
                               enum BT_POLICY_COMMAND cmd,
                               struct cras_bt_device* device,
                               struct cras_iodev* dev,
                               unsigned int arg1,
                               unsigned int arg2) {
  memset(msg, 0, sizeof(*msg));
  msg->header.type = CRAS_MAIN_BT_POLICY;
  msg->header.length = sizeof(*msg);
  msg->cmd = cmd;
  msg->device = device;
  msg->dev = dev;
  msg->arg1 = arg1;
  msg->arg2 = arg2;
}

static void init_bt_profile_switch_msg(struct bt_policy_msg* msg,
                                       struct bt_io_manager* mgr) {
  memset(msg, 0, sizeof(*msg));
  msg->header.type = CRAS_MAIN_BT_POLICY;
  msg->header.length = sizeof(*msg);
  msg->cmd = BT_POLICY_SWITCH_PROFILE;
  msg->mgr = mgr;
}

static void suspend_cb(struct cras_timer* timer, void* arg) {
  struct suspend_policy* policy = (struct suspend_policy*)arg;

  BTLOG(btlog, BT_DEV_SUSPEND_CB, policy->device->profiles,
        policy->suspend_reason);

  // Error log the reason so we can track them in user reports.
  switch (policy->suspend_reason) {
    case A2DP_LONG_TX_FAILURE:
      syslog(LOG_WARNING, "Suspend dev: A2DP long Tx failure");
      break;
    case A2DP_TX_FATAL_ERROR:
      syslog(LOG_WARNING, "Suspend dev: A2DP Tx fatal error");
      break;
    case CONN_WATCH_TIME_OUT:
      syslog(LOG_WARNING, "Suspend dev: Conn watch times out");
      break;
    case HFP_SCO_SOCKET_ERROR:
      syslog(LOG_WARNING, "Suspend dev: SCO socket error");
      break;
    case HFP_AG_START_FAILURE:
      syslog(LOG_WARNING, "Suspend dev: HFP AG start failure");
      break;
    case UNEXPECTED_PROFILE_DROP:
      syslog(LOG_WARNING, "Suspend dev: Unexpected profile drop");
      break;
  }

  cras_a2dp_suspend_connected_device(policy->device);
  cras_hfp_ag_suspend_connected_device(policy->device);
  cras_bt_device_disconnect(policy->device->conn, policy->device);

  DL_DELETE(suspend_policies, policy);
  free(policy);
}

static void schedule_suspend(
    struct cras_bt_device* device,
    unsigned int msec,
    enum cras_bt_policy_suspend_reason suspend_reason) {
  struct cras_tm* tm = cras_system_state_get_tm();
  struct suspend_policy* policy;

  DL_SEARCH_SCALAR(suspend_policies, policy, device, device);
  if (policy) {
    return;
  }

  policy = (struct suspend_policy*)calloc(1, sizeof(*policy));
  policy->device = device;
  policy->suspend_reason = suspend_reason;
  policy->timer = cras_tm_create_timer(tm, msec, suspend_cb, policy);
  DL_APPEND(suspend_policies, policy);
}

static void cancel_suspend(struct cras_bt_device* device) {
  struct cras_tm* tm = cras_system_state_get_tm();
  struct suspend_policy* policy;

  DL_SEARCH_SCALAR(suspend_policies, policy, device, device);
  if (policy) {
    cras_tm_cancel_timer(tm, policy->timer);
    DL_DELETE(suspend_policies, policy);
    free(policy);
  }
}

/* We're going to remove the dependency on cras_bt_device which is BlueZ
 * specific. For backward compatiblity during this migration, we have to live
 * with the complexity in checking both |msg->device| and |msg->mgr|.
 * TODO(hychao): clean up the validity check logic.
 */
static bool is_message_sender_valid(struct bt_policy_msg* msg) {
  if (!msg->device) {
    return bt_io_manager_exists(msg->mgr);
  }

  return cras_bt_device_valid(msg->device);
}

static void process_bt_policy_msg(struct cras_main_message* msg, void* arg) {
  struct bt_policy_msg* policy_msg = (struct bt_policy_msg*)msg;

  /* Before we handle the policy message, check if the sender(i.e the BT
   * headset) is still valid. It could have already been destroyed in
   * main thread for other reasons caused by BT stack. If that's the case
   * then just skip this message. */
  if (!is_message_sender_valid(policy_msg)) {
    return;
  }

  switch (policy_msg->cmd) {
    case BT_POLICY_SWITCH_PROFILE:
      switch_profile(policy_msg->mgr);
      break;
    case BT_POLICY_SCHEDULE_SUSPEND:
      schedule_suspend(policy_msg->device, policy_msg->arg1,
                       (enum cras_bt_policy_suspend_reason)policy_msg->arg2);
      break;
    case BT_POLICY_CANCEL_SUSPEND:
      cancel_suspend(policy_msg->device);
      break;
    default:
      break;
  }
}

int cras_bt_policy_switch_profile(struct bt_io_manager* mgr) {
  struct bt_policy_msg msg = CRAS_MAIN_MESSAGE_INIT;
  int rc;

  mgr->is_profile_switching = true;

  init_bt_profile_switch_msg(&msg, mgr);

  rc = cras_main_message_send((struct cras_main_message*)&msg);
  return rc;
}

int cras_bt_policy_schedule_suspend(
    struct cras_bt_device* device,
    unsigned int msec,
    enum cras_bt_policy_suspend_reason suspend_reason) {
  struct bt_policy_msg msg = CRAS_MAIN_MESSAGE_INIT;
  int rc;

  init_bt_policy_msg(&msg, BT_POLICY_SCHEDULE_SUSPEND, device, NULL, msec,
                     suspend_reason);
  rc = cras_main_message_send((struct cras_main_message*)&msg);
  return rc;
}

int cras_bt_policy_cancel_suspend(struct cras_bt_device* device) {
  struct bt_policy_msg msg = CRAS_MAIN_MESSAGE_INIT;
  int rc;

  init_bt_policy_msg(&msg, BT_POLICY_CANCEL_SUSPEND, device, NULL, 0, 0);
  rc = cras_main_message_send((struct cras_main_message*)&msg);
  return rc;
}

// Callback used to periodically check if supported profiles are connected.
static void conn_watch_cb(struct cras_timer* timer, void* arg) {
  struct cras_tm* tm;
  struct connection_watch* policy = (struct connection_watch*)arg;
  struct cras_bt_device* device = policy->device;
  int rc;
  bool a2dp_supported;
  bool a2dp_connected;
  bool hfp_supported;
  bool hfp_connected;

  BTLOG(btlog, BT_DEV_CONN_WATCH_CB, policy->retries_left, device->profiles);
  policy->timer = NULL;

  // Skip the callback if it is not an audio device.
  if (!device->profiles) {
    goto done_with_policy;
  }

  a2dp_supported =
      cras_bt_device_supports_profile(device, CRAS_BT_DEVICE_PROFILE_A2DP_SINK);
  a2dp_connected = cras_bt_device_is_profile_connected(
      device, CRAS_BT_DEVICE_PROFILE_A2DP_SINK);
  hfp_supported = cras_bt_device_supports_profile(
      device, CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);
  hfp_connected = cras_bt_device_is_profile_connected(
      device, CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);

  /* If not both A2DP and HFP are supported, simply wait for BlueZ
   * to notify us about the new connection.
   * Otherwise, when seeing one but not the other profile is connected,
   * send message to ask BlueZ to connect the pending one.
   */
  if (a2dp_supported && hfp_supported) {
    /* If both a2dp and hfp are not connected, do nothing. BlueZ
     * should be responsible to notify connection of one profile.
     */
    if (!a2dp_connected && hfp_connected) {
      cras_bt_device_connect_profile(device->conn, device, A2DP_SINK_UUID);
    }
    if (a2dp_connected && !hfp_connected) {
      cras_bt_device_connect_profile(device->conn, device, HFP_HF_UUID);
    }
  }

  /* If there's still a profile missing connection, arm the timer to
   * retry the logic in conn_watch_cb later, and return.  */
  if (a2dp_supported != a2dp_connected || hfp_supported != hfp_connected) {
    syslog(LOG_DEBUG, "conn_watch_retries: %d", policy->retries_left);

    if (--policy->retries_left) {
      tm = cras_system_state_get_tm();
      policy->timer =
          cras_tm_create_timer(tm, CONN_WATCH_PERIOD_MS, conn_watch_cb, policy);
      return;
    } else {
      syslog(LOG_WARNING, "Connection watch timeout.");
      schedule_suspend(device, 0, CONN_WATCH_TIME_OUT);
      goto done_with_policy;
    }
  }

  /* Expected profiles are all connected, no more connection watch
   * callback will be scheduled.
   * Base on the decision that we expose only the latest connected
   * BT audio device to user, treat all other connected devices as
   * conflict and remove them before we start A2DP/HFP of this device.
   */
  cras_bt_device_remove_conflict(device);

  if (cras_bt_device_is_profile_connected(device,
                                          CRAS_BT_DEVICE_PROFILE_A2DP_SINK)) {
    cras_a2dp_start(device);
  }

  if (cras_bt_device_is_profile_connected(
          device, CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE)) {
    rc = cras_hfp_ag_start(device);
    if (rc) {
      syslog(LOG_WARNING, "Start audio gateway failed, rc %d", rc);
      schedule_suspend(device, 0, HFP_AG_START_FAILURE);
    }
  }
  bt_io_manager_set_nodes_plugged(device->bt_io_mgr, 1);

done_with_policy:
  DL_DELETE(conn_watch_policies, policy);
  free(policy);
}

int cras_bt_policy_start_connection_watch(struct cras_bt_device* device) {
  struct cras_tm* tm = cras_system_state_get_tm();
  struct connection_watch* policy;

  DL_SEARCH_SCALAR(conn_watch_policies, policy, device, device);
  if (policy) {
    cras_tm_cancel_timer(tm, policy->timer);
  } else {
    policy = (struct connection_watch*)calloc(1, sizeof(*policy));
    policy->device = device;
    DL_APPEND(conn_watch_policies, policy);
  }
  policy->retries_left = CONN_WATCH_MAX_RETRIES;
  policy->timer =
      cras_tm_create_timer(tm, CONN_WATCH_PERIOD_MS, conn_watch_cb, policy);
  return 0;
}

int cras_bt_policy_stop_connection_watch(struct cras_bt_device* device) {
  struct cras_tm* tm = cras_system_state_get_tm();
  struct connection_watch* policy;

  DL_SEARCH_SCALAR(conn_watch_policies, policy, device, device);
  if (policy) {
    cras_tm_cancel_timer(tm, policy->timer);
    DL_DELETE(conn_watch_policies, policy);
    free(policy);
  }
  return 0;
}

void cras_bt_policy_remove_io_manager(struct bt_io_manager* mgr) {
  struct profile_switch_policy* policy;
  struct cras_tm* tm = cras_system_state_get_tm();

  DL_SEARCH_SCALAR(profile_switch_policies, policy, mgr, mgr);
  if (policy) {
    DL_DELETE(profile_switch_policies, policy);
    cras_tm_cancel_timer(tm, policy->timer);
    free(policy);
  }
}

void cras_bt_policy_remove_device(struct cras_bt_device* device) {
  cras_bt_policy_remove_io_manager(device->bt_io_mgr);

  // Clean up the bt_device only stuff.
  cancel_suspend(device);
  cras_bt_policy_stop_connection_watch(device);
}

void cras_bt_policy_start() {
  cras_main_message_add_handler(CRAS_MAIN_BT_POLICY, process_bt_policy_msg,
                                NULL);
}

void cras_bt_policy_stop() {
  cras_main_message_rm_handler(CRAS_MAIN_BT_POLICY);
}

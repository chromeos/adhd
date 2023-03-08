/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_bt_io.h"

#include <sys/time.h>
#include <syslog.h>

#include "cras/src/server/cras_bt_device.h"
#include "cras/src/server/cras_bt_policy.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_utf8.h"
#include "third_party/strlcpy/strlcpy.h"
#include "third_party/superfasthash/sfh.h"
#include "third_party/utlist/utlist.h"

#define DEFAULT_BT_DEVICE_NAME "BLUETOOTH"

/* Extends cras_ionode to hold bluetooth profile information
 * so that iodevs of different profile(A2DP or HFP) can be
 * associated with the same bt_io.
 */
struct bt_node {
  // The base class cras_ionode.
  struct cras_ionode base;
  // Pointer to the profile specific iodev.
  struct cras_iodev* profile_dev;
};

/* The structure represents a virtual input or output device of a
 * bluetooth audio device, speaker or headset for example. A node
 * will be added to this virtual iodev for each profile supported
 * by the bluetooth audio device.
 */
struct bt_io {
  // The base class cras_iodev
  struct cras_iodev base;
  // The index will give to the next node
  unsigned int next_node_id;
  // Pointer to the bt_io_manager that is responsible for
  // profile switching when |bt_io| opens or closes.
  struct bt_io_manager* mgr;
};

static struct bt_io_manager* bt_io_managers;

// Returns the active profile specific iodev.
static struct cras_iodev* active_profile_dev(const struct cras_iodev* iodev) {
  struct bt_node* active = (struct bt_node*)iodev->active_node;

  return active->profile_dev;
}

// Adds a profile specific iodev to btio.
static struct cras_ionode* add_profile_dev(struct cras_iodev* bt_iodev,
                                           struct cras_iodev* dev) {
  struct bt_node* n;
  struct bt_io* btio = (struct bt_io*)bt_iodev;

  n = (struct bt_node*)calloc(1, sizeof(*n));
  if (!n) {
    return NULL;
  }

  n->base.dev = bt_iodev;
  n->base.btflags = dev->active_node->btflags;
  n->base.idx = btio->next_node_id++;
  n->base.type = CRAS_NODE_TYPE_BLUETOOTH;
  n->base.volume = 100;
  n->base.stable_id = dev->info.stable_id;
  n->base.capture_gain = 0;
  gettimeofday(&n->base.plugged_time, NULL);

  strlcpy(n->base.name, dev->info.name, sizeof(n->base.name));
  n->profile_dev = dev;

  cras_iodev_add_node(bt_iodev, &n->base);
  return &n->base;
}

// Looks up for the node of given profile and returns if it exist or not.
static bool bt_io_has_a2dp(struct cras_iodev* bt_iodev) {
  struct cras_ionode* node;
  DL_FOREACH (bt_iodev->nodes, node) {
    if (node->btflags & CRAS_BT_FLAG_A2DP) {
      return true;
    }
  }
  return false;
}

static bool bt_io_has_hfp(struct cras_iodev* bt_iodev) {
  struct cras_ionode* node;
  DL_FOREACH (bt_iodev->nodes, node) {
    if (node->btflags & CRAS_BT_FLAG_HFP) {
      return true;
    }
  }
  return false;
}

int bt_io_manager_has_a2dp(struct bt_io_manager* mgr) {
  struct cras_iodev* odev = mgr->bt_iodevs[CRAS_STREAM_OUTPUT];

  // Check if there is an output iodev with A2DP node attached.
  return odev && bt_io_has_a2dp(odev);
}

void bt_io_manager_set_use_hardware_volume(struct bt_io_manager* mgr,
                                           int use_hardware_volume) {
  struct cras_iodev* iodev;

  iodev = mgr->bt_iodevs[CRAS_STREAM_OUTPUT];
  if (iodev) {
    /*
     * TODO(b/229031342): For BlueZ case, whether HFP uses software
     * volume is tied to AVRCP supported or not. We should fix this.
     */
    iodev->software_volume_needed = !use_hardware_volume;
  }
}

void bt_io_manager_update_hardware_volume(struct bt_io_manager* mgr,
                                          int volume) {
  struct cras_iodev* iodev;

  iodev = mgr->bt_iodevs[CRAS_STREAM_OUTPUT];
  if (iodev == NULL) {
    return;
  }

  iodev->active_node->volume = volume;
  cras_iodev_list_notify_node_volume(iodev->active_node);
}

/*
 * When A2DP is connected and appends iodev to |mgr| we prefer to use if
 * possible. And the logic to whether it's allowed is by checking if BT
 * input is now being used or not.
 */
static int can_switch_to_a2dp_when_connected(struct bt_io_manager* mgr) {
  struct cras_iodev* idev = mgr->bt_iodevs[CRAS_STREAM_INPUT];

  return bt_io_manager_has_a2dp(mgr) && (!idev || !cras_iodev_is_open(idev));
}

/* When bt_io opens or closes so that A2DP better serves the use case,
 * check with |btio->mgr| to see if that's possible and if so switches
 * the active profile to A2DP. */
static void possibly_switch_to_a2dp(struct bt_io_manager* mgr) {
  if (!bt_io_manager_has_a2dp(mgr)) {
    return;
  }
  mgr->active_btflag = CRAS_BT_FLAG_A2DP;
  cras_bt_policy_switch_profile(mgr);
}

static void switch_to_hfp(struct bt_io_manager* mgr) {
  mgr->active_btflag = CRAS_BT_FLAG_HFP;
  cras_bt_policy_switch_profile(mgr);
}

/* Checks if the condition is met to switch to a different profile based
 * on two rules:
 * (1) Prefer to use A2DP for output since the audio quality is better.
 * (2) Must use HFP for input since A2DP doesn't support audio input.
 *
 * If the profile switch happens, return non-zero error code, otherwise
 * return zero.
 */
static int open_dev(struct cras_iodev* iodev) {
  struct bt_io* btio = (struct bt_io*)iodev;
  struct cras_iodev* dev = active_profile_dev(iodev);
  int rc;

  // Force to use HFP if opening input dev.
  if (btio->mgr->active_btflag == CRAS_BT_FLAG_A2DP &&
      iodev->direction == CRAS_STREAM_INPUT) {
    switch_to_hfp(btio->mgr);
    return -EAGAIN;
  }

  // Make sure not to open when there is a pending profile-switch event.
  if (btio->mgr->is_profile_switching) {
    return -EAGAIN;
  }

  if (dev && dev->open_dev) {
    rc = dev->open_dev(dev);
    if (rc == 0) {
      return 0;
    }

    // If input iodev open fails, switch profile back to A2DP.
    if (iodev->direction == CRAS_STREAM_INPUT) {
      possibly_switch_to_a2dp(btio->mgr);
    }
    return rc;
  }

  return 0;
}

static int update_supported_formats(struct cras_iodev* iodev) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  int rc, length, i;

  if (!dev) {
    return -EINVAL;
  }

  if (dev->update_supported_formats) {
    rc = dev->update_supported_formats(dev);
    if (rc) {
      return rc;
    }
  }

  // Fill in the supported rates and channel counts.
  for (length = 0; dev->supported_rates[length]; length++)
    ;
  free(iodev->supported_rates);
  iodev->supported_rates =
      (size_t*)malloc((length + 1) * sizeof(*iodev->supported_rates));
  for (i = 0; i < length + 1; i++) {
    iodev->supported_rates[i] = dev->supported_rates[i];
  }

  for (length = 0; dev->supported_channel_counts[length]; length++)
    ;
  free(iodev->supported_channel_counts);
  iodev->supported_channel_counts =
      (size_t*)malloc((length + 1) * sizeof(*iodev->supported_channel_counts));
  for (i = 0; i < length + 1; i++) {
    iodev->supported_channel_counts[i] = dev->supported_channel_counts[i];
  }

  for (length = 0; dev->supported_formats[length]; length++)
    ;
  free(iodev->supported_formats);
  iodev->supported_formats = (snd_pcm_format_t*)malloc(
      (length + 1) * sizeof(*iodev->supported_formats));
  for (i = 0; i < length + 1; i++) {
    iodev->supported_formats[i] = dev->supported_formats[i];
  }

  // Record max supported channels into cras_iodev_info.
  iodev->info.max_supported_channels = dev->info.max_supported_channels;
  return 0;
}

/*
 * Sets up the volume to HFP or A2DP iodev using the cached volume
 * lastly updated by headset volume events.
 */
static void set_bt_volume(struct cras_iodev* iodev);

static int configure_dev(struct cras_iodev* iodev) {
  int rc;
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev) {
    return -EINVAL;
  }

  // Fill back the format iodev is using.
  if (dev->format == NULL) {
    dev->format = (struct cras_audio_format*)malloc(sizeof(*dev->format));
    if (!dev->format) {
      return -ENOMEM;
    }
    *dev->format = *iodev->format;
  }

  rc = dev->configure_dev(dev);
  if (rc) {
    return rc;
  }

  /* Apply the node's volume after profile dev is configured. Doing this
   * is necessary because the volume can not sync to hardware until
   * it is opened. */
  set_bt_volume(iodev);

  iodev->buffer_size = dev->buffer_size;
  iodev->min_buffer_level = dev->min_buffer_level;
  if (dev->start) {
    dev->state = CRAS_IODEV_STATE_OPEN;
  } else {
    dev->state = CRAS_IODEV_STATE_NO_STREAM_RUN;
  }

  return 0;
}

static int close_dev(struct cras_iodev* iodev) {
  struct bt_io* btio = (struct bt_io*)iodev;
  int rc;
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev) {
    return -EINVAL;
  }

  /* If input iodev is in open state and being closed, switch profile
   * from HFP to A2DP.
   * However, don't switch to A2DP if a profile-switch event is being queued,
   * which could be a special case where we want to simply restart an
   * existing HFP connection.
   */
  if (cras_iodev_is_open(iodev) &&
      btio->mgr->active_btflag == CRAS_BT_FLAG_HFP &&
      iodev->direction == CRAS_STREAM_INPUT &&
      !btio->mgr->is_profile_switching) {
    possibly_switch_to_a2dp(btio->mgr);
  }

  rc = dev->close_dev(dev);
  if (rc < 0) {
    return rc;
  }
  cras_iodev_free_format(iodev);
  dev->state = CRAS_IODEV_STATE_CLOSE;
  return 0;
}

static void set_bt_volume(struct cras_iodev* iodev) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev) {
    return;
  }

  // Do nothing for volume if this is an input iodev.
  if (iodev->direction == CRAS_STREAM_INPUT) {
    return;
  }

  if (dev->active_node) {
    dev->active_node->volume = iodev->active_node->volume;
  }

  /* The parent bt_iodev could set software_volume_needed flag for cases
   * that software volume provides better experience across profiles
   * (HFP and A2DP). Otherwise, use the profile specific implementation
   * to adjust volume. */
  if (dev->set_volume && !iodev->software_volume_needed) {
    dev->set_volume(dev);
  }
}

static int frames_queued(const struct cras_iodev* iodev,
                         struct timespec* tstamp) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev) {
    return -EINVAL;
  }
  return dev->frames_queued(dev, tstamp);
}

static int delay_frames(const struct cras_iodev* iodev) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev) {
    return -EINVAL;
  }
  return dev->delay_frames(dev);
}

static int get_buffer(struct cras_iodev* iodev,
                      struct cras_audio_area** area,
                      unsigned* frames) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev) {
    return -EINVAL;
  }
  return dev->get_buffer(dev, area, frames);
}

static int put_buffer(struct cras_iodev* iodev, unsigned nwritten) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev) {
    return -EINVAL;
  }
  return dev->put_buffer(dev, nwritten);
}

static int flush_buffer(struct cras_iodev* iodev) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev) {
    return -EINVAL;
  }
  return dev->flush_buffer(dev);
}

/* If the first private iodev doesn't match the active profile stored on
 * device, select to the correct private iodev.
 */
static void update_active_node(struct cras_iodev* iodev,
                               unsigned node_idx,
                               unsigned dev_enabled) {
  struct bt_io* btio = (struct bt_io*)iodev;
  struct cras_ionode* node;
  struct bt_node* active = (struct bt_node*)iodev->active_node;
  struct cras_iodev* dev;
  int rc;

  if (btio->mgr->active_btflag & active->base.btflags) {
    goto leave;
  }

  // Switch to the correct dev using active_btflag.
  DL_FOREACH (iodev->nodes, node) {
    struct bt_node* n = (struct bt_node*)node;
    if (n == active) {
      continue;
    }

    if (btio->mgr->active_btflag & n->base.btflags) {
      active->base.btflags = n->base.btflags;
      active->profile_dev = n->profile_dev;

      // Set volume for the new profile.
      set_bt_volume(iodev);
    }
  }

leave:
  dev = active_profile_dev(iodev);
  if (dev) {
    if (dev->update_active_node) {
      dev->update_active_node(dev, node_idx, dev_enabled);
    }
  }

  /* Update supported formats here to get the supported formats from the
   * new updated active profile dev.
   */
  rc = update_supported_formats(iodev);
  if (rc) {
    syslog(LOG_WARNING, "Failed to update supported formats, rc=%d", rc);
  }
}

static int output_underrun(struct cras_iodev* iodev) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev) {
    return -EINVAL;
  }

  if (dev->output_underrun) {
    dev->min_cb_level = iodev->min_cb_level;
    dev->max_cb_level = iodev->max_cb_level;
    dev->buffer_size = iodev->buffer_size;
    return dev->output_underrun(dev);
  }

  return 0;
}

static int no_stream(struct cras_iodev* iodev, int enable) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  int rc;

  if (!dev) {
    return -EINVAL;
  }

  if (dev->no_stream) {
    /*
     * Copy iodev->min_cb_level and iodev->max_cb_level from the
     * parent (i.e. bt_io).  no_stream() of hfp_alsa_iodev will
     * use them.
     * A2DP and HFP dev will use buffer and callback sizes to fill
     * zeros in no stream state.
     */
    dev->min_cb_level = iodev->min_cb_level;
    dev->max_cb_level = iodev->max_cb_level;
    dev->buffer_size = iodev->buffer_size;
    rc = dev->no_stream(dev, enable);
    if (rc < 0) {
      return rc;
    }
  }
  if (enable) {
    dev->state = CRAS_IODEV_STATE_NO_STREAM_RUN;
  } else {
    dev->state = CRAS_IODEV_STATE_NORMAL_RUN;
  }

  return 0;
}

static int is_free_running(const struct cras_iodev* iodev) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev) {
    return -EINVAL;
  }

  if (dev->is_free_running) {
    return dev->is_free_running(dev);
  }

  return 0;
}

static int start(struct cras_iodev* iodev) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  int rc;

  if (!dev) {
    return -EINVAL;
  }

  if (dev->start) {
    /*
     * Fill in properties set by audio thread.  A2dp or hfp
     * iodevs might need them to initialize states.
     */
    dev->min_cb_level = iodev->min_cb_level;
    dev->max_cb_level = iodev->max_cb_level;
    dev->buffer_size = iodev->buffer_size;

    rc = dev->start(dev);
    if (rc) {
      return rc;
    }
  }
  dev->state = CRAS_IODEV_STATE_NORMAL_RUN;
  return 0;
}

static unsigned int frames_to_play_in_sleep(struct cras_iodev* iodev,
                                            unsigned int* hw_level,
                                            struct timespec* hw_tstamp) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev || !dev->frames_to_play_in_sleep) {
    return cras_iodev_default_frames_to_play_in_sleep(iodev, hw_level,
                                                      hw_tstamp);
  }

  return dev->frames_to_play_in_sleep(dev, hw_level, hw_tstamp);
}

static int get_valid_frames(struct cras_iodev* iodev,
                            struct timespec* hw_tstamp) {
  struct cras_iodev* dev = active_profile_dev(iodev);
  if (!dev) {
    return -EINVAL;
  }

  if (dev->get_valid_frames) {
    return dev->get_valid_frames(dev, hw_tstamp);
  }

  return cras_iodev_frames_queued(iodev, hw_tstamp);
}

static enum CRAS_BT_FLAGS btflags_to_profile(uint32_t btflags) {
  if (btflags & CRAS_BT_FLAG_A2DP) {
    return CRAS_BT_FLAG_A2DP;
  }
  if (btflags & CRAS_BT_FLAG_HFP) {
    return CRAS_BT_FLAG_HFP;
  }
  return CRAS_BT_FLAG_NONE;
}

// Creates a bt_io iodev wrapper.
static struct cras_iodev* bt_io_create(struct bt_io_manager* mgr,
                                       struct cras_iodev* dev) {
  int err;
  struct bt_io* btio;
  struct cras_iodev* iodev;
  struct cras_ionode* node;
  struct bt_node* active;
  enum CRAS_BT_FLAGS profile_flag;

  if (!dev || !dev->active_node) {
    return NULL;
  }

  btio = (struct bt_io*)calloc(1, sizeof(*btio));
  if (!btio) {
    goto error;
  }
  btio->mgr = mgr;

  iodev = &btio->base;
  iodev->direction = dev->direction;
  strlcpy(iodev->info.name, dev->info.name, sizeof(iodev->info.name));
  iodev->info.stable_id = dev->info.stable_id;

  iodev->open_dev = open_dev;
  iodev->configure_dev = configure_dev;
  iodev->frames_queued = frames_queued;
  iodev->delay_frames = delay_frames;
  iodev->get_buffer = get_buffer;
  iodev->put_buffer = put_buffer;
  iodev->flush_buffer = flush_buffer;
  iodev->close_dev = close_dev;
  iodev->update_supported_formats = update_supported_formats;
  iodev->update_active_node = update_active_node;
  iodev->no_stream = no_stream;
  iodev->output_underrun = output_underrun;
  iodev->is_free_running = is_free_running;
  iodev->get_valid_frames = get_valid_frames;
  iodev->start = start;
  iodev->frames_to_play_in_sleep = frames_to_play_in_sleep;

  if (dev->direction == CRAS_STREAM_OUTPUT) {
    iodev->software_volume_needed = dev->software_volume_needed;
    iodev->set_volume = set_bt_volume;
  }

  /* Create the fake node so it's the only node exposed to UI, and
   * point it to the first profile dev. */
  active = (struct bt_node*)calloc(1, sizeof(*active));
  if (!active) {
    goto error;
  }
  active->base.dev = iodev;
  active->base.btflags = dev->active_node->btflags;
  active->base.idx = btio->next_node_id++;
  active->base.type = dev->active_node->type;
  active->base.volume = 100;
  active->base.stable_id = iodev->info.stable_id;
  active->base.ui_gain_scaler = 1.0f;
  /*
   * If the same headset is connected in wideband mode, we shall assign
   * a separate stable_id so the node priority/preference mechanism in
   * Chrome UI doesn't break.
   */
  if ((active->base.type == CRAS_NODE_TYPE_BLUETOOTH) &&
      (dev->direction == CRAS_STREAM_INPUT)) {
    active->base.stable_id =
        SuperFastHash((const char*)&active->base.type,
                      sizeof(active->base.type), active->base.stable_id);
  }

  active->base.btflags = dev->active_node->btflags;
  active->profile_dev = dev;
  strlcpy(active->base.name, dev->info.name, sizeof(active->base.name));
  // The node name exposed to UI should be a valid UTF8 string.
  if (!is_utf8_string(active->base.name)) {
    strlcpy(active->base.name, DEFAULT_BT_DEVICE_NAME,
            sizeof(active->base.name));
  }
  cras_iodev_add_node(iodev, &active->base);

  node = add_profile_dev(&btio->base, dev);
  if (node == NULL) {
    goto error;
  }

  /* Now we're creating a new cras_bt_io for |active->base.btflags|
   * check what does the bt_io_manager currently use as active_btflag.
   * If |active_btflag| hasn't been set at all, assign |btflag| to it.
   * Or if this is A2DP which we treat as higiher prioirty, set to A2DP
   * if we can.
   */
  profile_flag = btflags_to_profile(active->base.btflags);
  if (!btio->mgr->active_btflag ||
      (profile_flag == CRAS_BT_FLAG_A2DP &&
       can_switch_to_a2dp_when_connected(btio->mgr))) {
    btio->mgr->active_btflag = profile_flag;
  }

  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    err = cras_iodev_list_add_output(iodev);
  } else {
    err = cras_iodev_list_add_input(iodev);
  }
  if (err) {
    goto error;
  }

  cras_iodev_set_active_node(iodev, &active->base);
  return &btio->base;

error:
  if (btio) {
    free(btio);
  }
  return NULL;
}

static void bt_io_free_resources(struct cras_iodev* bt_iodev) {
  struct cras_ionode* node;
  struct bt_node* n;

  free(bt_iodev->supported_rates);
  free(bt_iodev->supported_channel_counts);
  free(bt_iodev->supported_formats);

  DL_FOREACH (bt_iodev->nodes, node) {
    n = (struct bt_node*)node;
    cras_iodev_rm_node(bt_iodev, node);
    free(n);
  }

  cras_iodev_free_resources(bt_iodev);
}

// Destroys a bt_io iodev wrapper.
static void bt_io_destroy(struct cras_iodev* bt_iodev) {
  int rc;
  struct bt_io* btio = (struct bt_io*)bt_iodev;

  if (bt_iodev->direction == CRAS_STREAM_OUTPUT) {
    rc = cras_iodev_list_rm_output(bt_iodev);
  } else {
    rc = cras_iodev_list_rm_input(bt_iodev);
  }
  if (rc == -EBUSY) {
    return;
  }

  bt_io_free_resources(bt_iodev);
  free(btio);
}

static int bt_io_append(struct cras_iodev* bt_iodev, struct cras_iodev* dev) {
  struct cras_ionode* node;
  struct bt_io* btio = (struct bt_io*)bt_iodev;

  node = add_profile_dev(bt_iodev, dev);
  if (!node) {
    return -ENOMEM;
  }

  // TODO(hychao): refine below after BT stop sending asynchronous
  // A2DP and HFP connection to CRAS.
  if ((node->btflags & CRAS_BT_FLAG_A2DP) &&
      can_switch_to_a2dp_when_connected(btio->mgr)) {
    possibly_switch_to_a2dp(btio->mgr);
  }
  return 0;
}

static int bt_io_remove(struct cras_iodev* bt_iodev, struct cras_iodev* dev) {
  struct cras_ionode* node;
  struct bt_node* btnode;

  DL_FOREACH (bt_iodev->nodes, node) {
    btnode = (struct bt_node*)node;
    if (btnode->profile_dev != dev) {
      continue;
    }

    /* If this is the active node, reset it. Otherwise delete
     * this node. */
    if (node == bt_iodev->active_node) {
      btnode->profile_dev = NULL;
      btnode->base.btflags = CRAS_BT_FLAG_NONE;
    } else {
      DL_DELETE(bt_iodev->nodes, node);
      free(node);
    }
  }

  /* The node of active profile could have been removed, update it.
   * Return err when fail to locate the active profile dev. */
  update_active_node(bt_iodev, 0, 1);
  btnode = (struct bt_node*)bt_iodev->active_node;
  if ((btnode->base.btflags == CRAS_BT_FLAG_NONE) ||
      (btnode->profile_dev == NULL)) {
    return -EINVAL;
  }

  return 0;
}

struct bt_io_manager* bt_io_manager_create() {
  struct bt_io_manager* mgr;

  mgr = (struct bt_io_manager*)calloc(1, sizeof(*mgr));
  if (mgr == NULL) {
    syslog(LOG_ERR, "No memory to create bt_io_manager");
    return NULL;
  }
  DL_APPEND(bt_io_managers, mgr);
  return mgr;
}

void bt_io_manager_destroy(struct bt_io_manager* mgr) {
  DL_DELETE(bt_io_managers, mgr);
  if (mgr->bt_iodevs[CRAS_STREAM_INPUT]) {
    syslog(LOG_WARNING, "Potential input bt_iodev leak");
  }
  if (mgr->bt_iodevs[CRAS_STREAM_OUTPUT]) {
    syslog(LOG_WARNING, "Potential output bt_iodev leak");
  }

  free(mgr);
}

bool bt_io_manager_exists(struct bt_io_manager* target) {
  struct bt_io_manager* mgr;
  DL_FOREACH (bt_io_managers, mgr) {
    if (mgr == target) {
      return true;
    }
  }
  return false;
}

/*
 * Sets the audio nodes to 'plugged' means UI can select it and open it
 * for streams. Sets to 'unplugged' to hide these nodes from UI, when device
 * disconnects in progress.
 */
void bt_io_manager_set_nodes_plugged(struct bt_io_manager* mgr, int plugged) {
  struct cras_iodev* iodev;

  iodev = mgr->bt_iodevs[CRAS_STREAM_INPUT];
  if (iodev) {
    cras_iodev_set_node_plugged(iodev->active_node, plugged);
  }

  iodev = mgr->bt_iodevs[CRAS_STREAM_OUTPUT];
  if (iodev) {
    cras_iodev_set_node_plugged(iodev->active_node, plugged);
  }
}

void bt_io_manager_append_iodev(struct bt_io_manager* mgr,
                                struct cras_iodev* iodev,
                                enum CRAS_BT_FLAGS pflag) {
  struct cras_iodev* bt_iodev;

  bt_iodev = mgr->bt_iodevs[iodev->direction];

  if (!(iodev->active_node->btflags & pflag)) {
    syslog(LOG_WARNING, "Incorrect btflags %.4x for dev %s as profile %.4x",
           iodev->active_node->btflags, iodev->info.name, pflag);
    return;
  }

  if (bt_iodev) {
    bool exists = false;
    switch (pflag) {
      case CRAS_BT_FLAG_A2DP:
        exists = bt_io_has_a2dp(bt_iodev);
        break;
      case CRAS_BT_FLAG_HFP:
        exists = bt_io_has_hfp(bt_iodev);
        break;
      default:
        return;  // -EINVAL
    }

    if (exists) {
      return;
    }

    bt_io_append(bt_iodev, iodev);
  } else {
    mgr->bt_iodevs[iodev->direction] = bt_io_create(mgr, iodev);
  }
}

void bt_io_manager_remove_iodev(struct bt_io_manager* mgr,
                                struct cras_iodev* iodev) {
  struct cras_iodev* bt_iodev;
  struct cras_ionode* node;
  struct bt_node *active, *btnode;
  unsigned int new_flag = CRAS_BT_FLAG_NONE;
  int rc;

  bt_io_manager_set_nodes_plugged(mgr, 0);

  bt_iodev = mgr->bt_iodevs[iodev->direction];
  if (bt_iodev == NULL) {
    return;
  }

  // Check what will be the fallback profile if we remove |iodev|.
  active = (struct bt_node*)bt_iodev->active_node;
  if (active->profile_dev == iodev) {
    DL_FOREACH (bt_iodev->nodes, node) {
      btnode = (struct bt_node*)node;
      /* Skip the active node and the node we're trying
       * to remove. */
      if (btnode == active || btnode->profile_dev == iodev) {
        continue;
      }
      new_flag = btnode->base.btflags;
      break;
    }
  } else {
    new_flag = active->base.btflags;
  }
  if (new_flag == CRAS_BT_FLAG_NONE) {
    goto destroy_bt_io;
  }

  /* If the check result |new_flag| doesn't match with the active
   * btflags we are currently using, switch to the profile specified
   * by |new_flag| before actually remove the iodev.
   */
  if (new_flag != active->base.btflags) {
    mgr->active_btflag = btflags_to_profile(new_flag);

    // TODO(hychao): remove below code after BT remove HFP
    // and A2DP from CRAS in one synchronous event.
    cras_bt_policy_switch_profile(mgr);
  }
  rc = bt_io_remove(bt_iodev, iodev);
  if (rc) {
    syslog(LOG_WARNING, "Fail to fallback to profile %u", new_flag);
    goto destroy_bt_io;
  }

  return;

destroy_bt_io:
  mgr->bt_iodevs[iodev->direction] = NULL;
  bt_io_destroy(bt_iodev);

  if (!mgr->bt_iodevs[CRAS_STREAM_INPUT] &&
      !mgr->bt_iodevs[CRAS_STREAM_OUTPUT]) {
    mgr->active_btflag = CRAS_BT_FLAG_NONE;
  }
}

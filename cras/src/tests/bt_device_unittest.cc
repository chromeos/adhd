// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

extern "C" {
#include "cras/src/server/cras_bt_constants.h"
#include "cras/src/server/cras_bt_device.h"
#include "cras/src/server/cras_bt_io.h"
#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_bt_policy.h"
#include "cras/src/server/cras_iodev.h"

#define FAKE_OBJ_PATH "/obj/path"
}

static unsigned int bt_io_manager_append_iodev_called;
static unsigned int bt_io_manager_remove_iodev_called;
static int cras_a2dp_start_called;
static int cras_a2dp_suspend_connected_device_called;
static int cras_hfp_ag_remove_conflict_called;
static int cras_hfp_ag_start_called;
static int cras_hfp_ag_suspend_connected_device_called;
static int dbus_message_new_method_call_called;
static const char* dbus_message_new_method_call_method;
static struct cras_bt_device* cras_a2dp_connected_device_ret;
static struct cras_bt_device* cras_a2dp_suspend_connected_device_dev;
static int cras_bt_policy_schedule_suspend_called;
static int cras_bt_policy_cancel_suspend_called;
static int cras_bt_policy_start_connection_watch_called;
static int cras_bt_policy_stop_connection_watch_called;

struct MockDBusMessage {
  int type;
  void* value;
  MockDBusMessage* next;
  MockDBusMessage* recurse;
};

void ResetStubData() {
  cras_a2dp_start_called = 0;
  cras_a2dp_suspend_connected_device_called = 0;
  cras_hfp_ag_remove_conflict_called = 0;
  cras_hfp_ag_start_called = 0;
  cras_hfp_ag_suspend_connected_device_called = 0;
  dbus_message_new_method_call_method = NULL;
  dbus_message_new_method_call_called = 0;
  cras_a2dp_connected_device_ret = NULL;
  cras_bt_policy_schedule_suspend_called = 0;
  cras_bt_policy_cancel_suspend_called = 0;
  cras_bt_policy_start_connection_watch_called = 0;
  cras_bt_policy_stop_connection_watch_called = 0;
}

static void FreeMockDBusMessage(MockDBusMessage* head) {
  if (head->next != NULL) {
    FreeMockDBusMessage(head->next);
  }
  if (head->recurse != NULL) {
    FreeMockDBusMessage(head->recurse);
  }
  if (head->type == DBUS_TYPE_STRING) {
    free((char*)head->value);
  }
  delete head;
}

static struct MockDBusMessage* NewMockDBusUuidMessage(const char* uuid) {
  MockDBusMessage* msg = new MockDBusMessage{DBUS_TYPE_ARRAY, NULL};
  MockDBusMessage* dict =
      new MockDBusMessage{DBUS_TYPE_STRING, (void*)strdup("UUIDs")};
  MockDBusMessage* variant = new MockDBusMessage{DBUS_TYPE_ARRAY, NULL};
  MockDBusMessage* uuid_var =
      new MockDBusMessage{DBUS_TYPE_STRING, (void*)strdup(uuid)};

  msg->recurse = dict;
  dict->next = new MockDBusMessage{DBUS_TYPE_INVALID, NULL};
  dict->next->recurse = variant;

  variant->recurse = uuid_var;
  return msg;
}

static struct MockDBusMessage* NewMockDBusConnectedMessage(long connected) {
  MockDBusMessage* msg = new MockDBusMessage{DBUS_TYPE_ARRAY, NULL};
  MockDBusMessage* dict =
      new MockDBusMessage{DBUS_TYPE_STRING, (void*)strdup("Connected")};
  MockDBusMessage* variant =
      new MockDBusMessage{DBUS_TYPE_BOOLEAN, (void*)connected};

  msg->recurse = dict;
  dict->next = new MockDBusMessage{DBUS_TYPE_INVALID, NULL};
  dict->next->recurse = variant;
  return msg;
}

namespace {

class BtDeviceTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    bt_iodev1.direction = CRAS_STREAM_OUTPUT;
    bt_iodev1.update_active_node = update_active_node;
    bt_iodev2.direction = CRAS_STREAM_INPUT;
    bt_iodev2.update_active_node = update_active_node;
    d1_.direction = CRAS_STREAM_OUTPUT;
    d1_.update_active_node = update_active_node;
    d2_.direction = CRAS_STREAM_OUTPUT;
    d2_.update_active_node = update_active_node;
    d3_.direction = CRAS_STREAM_INPUT;
    d3_.update_active_node = update_active_node;
    btlog = cras_bt_event_log_init();
  }

  virtual void TearDown() { cras_bt_event_log_deinit(btlog); }

  static void update_active_node(struct cras_iodev* iodev,
                                 unsigned node_idx,
                                 unsigned dev_enabled) {}

  struct cras_iodev bt_iodev1;
  struct cras_iodev bt_iodev2;
  struct cras_iodev d3_;
  struct cras_iodev d2_;
  struct cras_iodev d1_;
};

TEST(BtDeviceSuite, CreateBtDevice) {
  struct cras_bt_device *device, *device2;
  struct cras_bt_device* inval_dev;

  device = cras_bt_device_create(NULL, FAKE_OBJ_PATH);
  EXPECT_NE((void*)NULL, device);

  device = cras_bt_device_get(FAKE_OBJ_PATH);
  EXPECT_NE((void*)NULL, device);
  EXPECT_EQ(1, cras_bt_device_valid(device));

  // Pick an address that is not a valid device for sure.
  inval_dev = reinterpret_cast<struct cras_bt_device*>(device + 0x1);
  EXPECT_EQ(0, cras_bt_device_valid(inval_dev));

  device2 = cras_bt_device_create(NULL, "/another/obj");
  EXPECT_NE((void*)NULL, device2);
  EXPECT_EQ(1, cras_bt_device_valid(device2));
  EXPECT_EQ(1, cras_bt_device_valid(device));

  cras_bt_device_remove(device);
  device = cras_bt_device_get(FAKE_OBJ_PATH);
  EXPECT_EQ((void*)NULL, device);
  EXPECT_EQ(0, cras_bt_device_valid(device));
  EXPECT_EQ(1, cras_bt_device_valid(device2));

  cras_bt_device_remove(device2);
  EXPECT_EQ(0, cras_bt_device_valid(device2));
}

TEST_F(BtDeviceTestSuite, AppendRmIodev) {
  struct cras_bt_device* device;
  device = cras_bt_device_create(NULL, FAKE_OBJ_PATH);

  cras_bt_device_append_iodev(device, &d1_, CRAS_BT_FLAG_A2DP);
  EXPECT_EQ(1, bt_io_manager_append_iodev_called);

  cras_bt_device_append_iodev(device, &d2_, CRAS_BT_FLAG_HFP);
  EXPECT_EQ(2, bt_io_manager_append_iodev_called);

  cras_bt_device_rm_iodev(device, &d2_);
  EXPECT_EQ(1, bt_io_manager_remove_iodev_called);

  cras_bt_device_rm_iodev(device, &d1_);
  EXPECT_EQ(2, bt_io_manager_remove_iodev_called);

  cras_bt_device_remove(device);
}

TEST_F(BtDeviceTestSuite, AddUuidAfterConnected) {
  struct cras_bt_device* device;
  struct MockDBusMessage *msg_root, *cur;
  ResetStubData();

  device = cras_bt_device_create(NULL, FAKE_OBJ_PATH);
  EXPECT_NE((void*)NULL, device);

  cras_bt_device_set_supported_profiles(device,
                                        CRAS_BT_DEVICE_PROFILE_A2DP_SINK);

  cur = msg_root = NewMockDBusConnectedMessage(1);
  cras_bt_device_update_properties(device, (DBusMessageIter*)&cur, NULL);
  EXPECT_EQ(1, cras_bt_policy_start_connection_watch_called);
  FreeMockDBusMessage(msg_root);

  /* UUIDs updated with new profile CRAS cares. Expect connection
   * watch policy restarts because ofr that. */
  cur = msg_root = NewMockDBusUuidMessage(HFP_HF_UUID);
  cras_bt_device_update_properties(device, (DBusMessageIter*)&cur, NULL);
  EXPECT_EQ(2, cras_bt_policy_start_connection_watch_called);

  cras_bt_device_remove(device);
  FreeMockDBusMessage(msg_root);
}

TEST_F(BtDeviceTestSuite, DevRemoveConflict) {
  struct cras_bt_device* device;

  ResetStubData();

  device = cras_bt_device_create(NULL, FAKE_OBJ_PATH);
  EXPECT_NE((void*)NULL, device);

  cras_bt_device_set_supported_profiles(
      device,
      CRAS_BT_DEVICE_PROFILE_A2DP_SINK | CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);

  // Fake that a different device already connected with A2DP
  cras_a2dp_connected_device_ret =
      reinterpret_cast<struct cras_bt_device*>(0x99);
  cras_bt_device_remove_conflict(device);

  // Expect check conflict in HFP AG and A2DP.
  EXPECT_EQ(1, cras_hfp_ag_remove_conflict_called);
  EXPECT_EQ(1, cras_a2dp_suspend_connected_device_called);
  EXPECT_EQ(cras_a2dp_suspend_connected_device_dev,
            cras_a2dp_connected_device_ret);

  cras_bt_device_remove(device);
}

TEST_F(BtDeviceTestSuite, A2dpDropped) {
  struct cras_bt_device* device;
  struct MockDBusMessage *msg_root, *cur;

  ResetStubData();

  device = cras_bt_device_create(NULL, FAKE_OBJ_PATH);
  EXPECT_NE((void*)NULL, device);

  cras_bt_device_set_supported_profiles(
      device,
      CRAS_BT_DEVICE_PROFILE_A2DP_SINK | CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);

  cur = msg_root = NewMockDBusConnectedMessage(1);
  cras_bt_device_update_properties(device, (DBusMessageIter*)&cur, NULL);

  cras_bt_device_notify_profile_dropped(device,
                                        CRAS_BT_DEVICE_PROFILE_A2DP_SINK);
  EXPECT_EQ(1, cras_bt_policy_schedule_suspend_called);

  cras_bt_device_remove(device);
  FreeMockDBusMessage(msg_root);
}

TEST_F(BtDeviceTestSuite, DevConnectDisconnectBackToBack) {
  struct cras_bt_device* device;
  struct MockDBusMessage *msg_root, *cur;

  ResetStubData();

  device = cras_bt_device_create(NULL, FAKE_OBJ_PATH);
  EXPECT_NE((void*)NULL, device);

  cras_bt_device_set_supported_profiles(
      device,
      CRAS_BT_DEVICE_PROFILE_A2DP_SINK | CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);

  cur = msg_root = NewMockDBusConnectedMessage(1);
  cras_bt_device_update_properties(device, (DBusMessageIter*)&cur, NULL);
  EXPECT_EQ(1, cras_bt_policy_start_connection_watch_called);
  FreeMockDBusMessage(msg_root);

  cras_bt_device_a2dp_configured(device);
  cras_bt_device_audio_gateway_initialized(device);

  // Expect suspend timer is scheduled.
  cras_bt_device_notify_profile_dropped(device,
                                        CRAS_BT_DEVICE_PROFILE_A2DP_SINK);
  EXPECT_EQ(1, cras_bt_policy_schedule_suspend_called);

  // Another profile drop should trigger call to policy schedule suspend.
  cras_bt_device_notify_profile_dropped(device,
                                        CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);
  EXPECT_EQ(2, cras_bt_policy_schedule_suspend_called);

  cur = msg_root = NewMockDBusConnectedMessage(0);
  cras_bt_device_update_properties(device, (DBusMessageIter*)&cur, NULL);

  // When BlueZ reports headset disconnection, cancel the pending timer.
  EXPECT_EQ(1, cras_bt_policy_cancel_suspend_called);
  FreeMockDBusMessage(msg_root);

  // Headset connects again.
  cur = msg_root = NewMockDBusConnectedMessage(1);
  cras_bt_device_update_properties(device, (DBusMessageIter*)&cur, NULL);
  EXPECT_EQ(2, cras_bt_policy_start_connection_watch_called);
  FreeMockDBusMessage(msg_root);

  /* Headset disconnects, later profile drop events shouldn't trigger
   * suspend timer because headset is already in disconnected stats.
   */
  cur = msg_root = NewMockDBusConnectedMessage(0);
  cras_bt_device_update_properties(device, (DBusMessageIter*)&cur, NULL);
  FreeMockDBusMessage(msg_root);

  cras_bt_policy_schedule_suspend_called = 0;
  cras_bt_device_notify_profile_dropped(device,
                                        CRAS_BT_DEVICE_PROFILE_A2DP_SINK);
  EXPECT_EQ(0, cras_bt_policy_schedule_suspend_called);
  cras_bt_device_notify_profile_dropped(device,
                                        CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);
  EXPECT_EQ(0, cras_bt_policy_schedule_suspend_called);

  cras_bt_device_remove(device);
}

// Stubs
extern "C" {

struct cras_bt_event_log* btlog;

// From bt_io
struct bt_io_manager* bt_io_manager_create() {
  return reinterpret_cast<struct bt_io_manager*>(0x123);
}

void bt_io_manager_set_use_hardware_volume(struct bt_io_manager* mgr,
                                           int use_hardware_volume) {}

void bt_io_manager_destroy(struct bt_io_manager* mgr) {}

void bt_io_manager_append_iodev(struct bt_io_manager* mgr,
                                struct cras_iodev* iodev,
                                enum CRAS_BT_FLAGS btflag) {
  bt_io_manager_append_iodev_called++;
}

void bt_io_manager_remove_iodev(struct bt_io_manager* mgr,
                                struct cras_iodev* iodev) {
  bt_io_manager_remove_iodev_called++;
}

// From bt_adapter
struct cras_bt_adapter* cras_bt_adapter_get(const char* object_path) {
  return NULL;
}
const char* cras_bt_adapter_address(const struct cras_bt_adapter* adapter) {
  return NULL;
}

int cras_bt_adapter_on_usb(struct cras_bt_adapter* adapter) {
  return 1;
}

// From bt_profile
void cras_bt_profile_on_device_disconnected(struct cras_bt_device* device) {}

// From hfp_ag_profile
struct hfp_slc_handle* cras_hfp_ag_get_slc(struct cras_bt_device* device) {
  return NULL;
}

void cras_hfp_ag_suspend_connected_device(struct cras_bt_device* device) {
  cras_hfp_ag_suspend_connected_device_called++;
}

void cras_a2dp_suspend_connected_device(struct cras_bt_device* device) {
  cras_a2dp_suspend_connected_device_called++;
  cras_a2dp_suspend_connected_device_dev = device;
}

void cras_a2dp_start(struct cras_bt_device* device) {
  cras_a2dp_start_called++;
}

struct cras_bt_device* cras_a2dp_connected_device() {
  return cras_a2dp_connected_device_ret;
}

int cras_hfp_ag_remove_conflict(struct cras_bt_device* device) {
  cras_hfp_ag_remove_conflict_called++;
  return 0;
}

int cras_hfp_ag_start(struct cras_bt_device* device) {
  cras_hfp_ag_start_called++;
  return 0;
}

void cras_hfp_ag_suspend() {}

// From hfp_slc
int hfp_event_speaker_gain(struct hfp_slc_handle* handle, int gain) {
  return 0;
}

// From iodev_list

int cras_iodev_open(struct cras_iodev* dev,
                    unsigned int cb_level,
                    const struct cras_audio_format* fmt) {
  return 0;
}

int cras_iodev_close(struct cras_iodev* dev) {
  return 0;
}

int cras_iodev_list_dev_is_enabled(const struct cras_iodev* dev) {
  return 0;
}

void cras_iodev_list_suspend_dev(struct cras_iodev* dev) {}

void cras_iodev_list_resume_dev(struct cras_iodev* dev) {}

void cras_iodev_list_notify_node_volume(struct cras_ionode* node) {}

int cras_bt_policy_switch_profile(struct bt_io_manager* mgr) {
  return 0;
}

int cras_bt_policy_schedule_suspend(
    struct cras_bt_device* device,
    unsigned int msec,
    enum cras_bt_policy_suspend_reason suspend_reason) {
  cras_bt_policy_schedule_suspend_called++;
  return 0;
}

// Cancels any scheduled suspension of device.
int cras_bt_policy_cancel_suspend(struct cras_bt_device* device) {
  cras_bt_policy_cancel_suspend_called++;
  return 0;
}

void cras_bt_policy_remove_device(struct cras_bt_device* device) {}

int cras_bt_policy_start_connection_watch(struct cras_bt_device* device) {
  cras_bt_policy_start_connection_watch_called++;
  return 0;
}
int cras_bt_policy_stop_connection_watch(struct cras_bt_device* device) {
  cras_bt_policy_stop_connection_watch_called++;
  return 0;
}

DBusMessage* dbus_message_new_method_call(const char* destination,
                                          const char* path,
                                          const char* iface,
                                          const char* method) {
  dbus_message_new_method_call_called++;
  dbus_message_new_method_call_method = method;
  return reinterpret_cast<DBusMessage*>(0x456);
}

void dbus_message_unref(DBusMessage* message) {}

dbus_bool_t dbus_message_append_args(DBusMessage* message,
                                     int first_arg_type,
                                     ...) {
  return true;
}

dbus_bool_t dbus_connection_send_with_reply(DBusConnection* connection,
                                            DBusMessage* message,
                                            DBusPendingCall** pending_return,
                                            int timeout_milliseconds) {
  return true;
}

dbus_bool_t dbus_pending_call_set_notify(DBusPendingCall* pending,
                                         DBusPendingCallNotifyFunction function,
                                         void* user_data,
                                         DBusFreeFunction free_user_data) {
  return true;
}

void dbus_message_iter_recurse(DBusMessageIter* iter, DBusMessageIter* sub) {
  MockDBusMessage* msg = *(MockDBusMessage**)iter;
  MockDBusMessage** cur = (MockDBusMessage**)sub;
  *cur = msg->recurse;
}

dbus_bool_t dbus_message_iter_next(DBusMessageIter* iter) {
  MockDBusMessage** cur = (MockDBusMessage**)iter;
  MockDBusMessage* msg = *cur;
  *cur = msg->next;
  return true;
}

int dbus_message_iter_get_arg_type(DBusMessageIter* iter) {
  MockDBusMessage* msg;

  if (iter == NULL) {
    return DBUS_TYPE_INVALID;
  }

  msg = *(MockDBusMessage**)iter;
  if (msg == NULL) {
    return DBUS_TYPE_INVALID;
  }

  return msg->type;
}

char* dbus_message_iter_get_signature(DBusMessageIter* iter) {
  MockDBusMessage* msg;

  if (iter == NULL) {
    return (char*)"";
  }
  msg = *(MockDBusMessage**)iter;
  if (msg == NULL) {
    return (char*)"";
  }
  if ((msg->type == DBUS_TYPE_ARRAY) && msg->recurse &&
      (msg->recurse->type == DBUS_TYPE_STRING)) {
    return (char*)"as";
  }
  return (char*)"";
}

void dbus_message_iter_get_basic(DBusMessageIter* iter, void* value) {
  MockDBusMessage* msg = *(MockDBusMessage**)iter;
  switch (msg->type) {
    case DBUS_TYPE_BOOLEAN:
      memcpy(value, &msg->value, sizeof(int));
      break;
    case DBUS_TYPE_STRING:
      memcpy(value, &msg->value, sizeof(char*));
      break;
  }
}

}  // extern "C"
}  // namespace

// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

extern "C" {
#include "cras/src/server/cras_bt_manager.h"
}

static int cras_hfp_ag_profile_create_called;
static int cras_hfp_ag_profile_destroy_called;
static int cras_telephony_start_called;
static int cras_telephony_stop_called;
static int cras_a2dp_endpoint_create_called;
static int cras_a2dp_endpoint_destroy_called;
static int cras_bt_player_create_called;
static int cras_bt_player_destroy_called;
static int cras_bt_unregister_battery_provider_called;
static int dbus_connection_add_filter_called;
static int dbus_connection_remove_filter_called;
static int cras_bt_policy_start_called;
static int cras_bt_policy_stop_called;
static int fake_start_called;
static void fake_start(bt_stack* s) {
  fake_start_called++;
}
static int fake_stop_called;
static void fake_stop(bt_stack* s) {
  fake_stop_called++;
}
static bt_stack fake_stack;

void ResetStubData() {
  cras_hfp_ag_profile_create_called = 0;
  cras_hfp_ag_profile_destroy_called = 0;
  cras_telephony_start_called = 0;
  cras_telephony_stop_called = 0;
  cras_a2dp_endpoint_create_called = 0;
  cras_a2dp_endpoint_destroy_called = 0;
  cras_bt_player_create_called = 0;
  cras_bt_player_destroy_called = 0;
  cras_bt_unregister_battery_provider_called = 0;
  cras_bt_policy_start_called = 0;
  cras_bt_policy_stop_called = 0;
  dbus_connection_add_filter_called = 0;
  dbus_connection_remove_filter_called = 0;
  fake_start_called = 0;
  fake_stop_called = 0;
  fake_stack.start = fake_start;
  fake_stack.stop = fake_stop;
}

namespace {

TEST(BtManager, StartStop) {
  // Make sure static variables goes back to default.
  cras_bt_switch_default_stack();

  ResetStubData();
  cras_bt_start(NULL, 0x00);
  ASSERT_EQ(1, cras_hfp_ag_profile_create_called);
  ASSERT_EQ(1, cras_telephony_start_called);
  ASSERT_EQ(1, cras_a2dp_endpoint_create_called);
  ASSERT_EQ(1, cras_bt_player_create_called);
  ASSERT_EQ(0, cras_bt_player_destroy_called);
  ASSERT_EQ(0, dbus_connection_remove_filter_called);
  ASSERT_LT(0, dbus_connection_add_filter_called);
  ASSERT_EQ(1, cras_bt_policy_start_called);
  ASSERT_EQ(0, cras_bt_policy_stop_called);

  cras_bt_stop(NULL);
  ASSERT_LT(0, dbus_connection_remove_filter_called);
  ASSERT_EQ(1, cras_bt_policy_stop_called);
  ASSERT_EQ(1, cras_hfp_ag_profile_destroy_called);
  ASSERT_EQ(1, cras_telephony_stop_called);
  ASSERT_EQ(1, cras_a2dp_endpoint_destroy_called);
  ASSERT_EQ(1, cras_bt_player_destroy_called);
  ASSERT_EQ(1, cras_bt_unregister_battery_provider_called);
}

TEST(BtManager, SwitchStackThenBackToDefault) {
  // Make sure static variables goes back to default.
  cras_bt_switch_default_stack();

  ResetStubData();
  cras_bt_start(NULL, 0x00);
  ASSERT_EQ(0, dbus_connection_remove_filter_called);
  ASSERT_EQ(1, cras_bt_policy_start_called);
  ASSERT_EQ(1, cras_hfp_ag_profile_create_called);
  ASSERT_EQ(1, cras_telephony_start_called);
  ASSERT_EQ(1, cras_a2dp_endpoint_create_called);
  ASSERT_EQ(1, cras_bt_player_create_called);

  cras_bt_switch_stack(&fake_stack);
  ASSERT_LT(0, dbus_connection_remove_filter_called);
  ASSERT_EQ(1, fake_start_called);
  ASSERT_EQ(0, fake_stop_called);
  ASSERT_EQ(1, cras_bt_policy_stop_called);
  ASSERT_EQ(1, cras_hfp_ag_profile_destroy_called);
  ASSERT_EQ(1, cras_telephony_stop_called);
  ASSERT_EQ(1, cras_a2dp_endpoint_destroy_called);
  ASSERT_EQ(1, cras_bt_player_destroy_called);
  ASSERT_EQ(1, cras_bt_unregister_battery_provider_called);

  cras_bt_switch_default_stack();
  ASSERT_EQ(1, fake_stop_called);
  ASSERT_EQ(2, cras_bt_policy_start_called);
  ASSERT_EQ(2, cras_hfp_ag_profile_create_called);
  ASSERT_EQ(2, cras_telephony_start_called);
  ASSERT_EQ(2, cras_a2dp_endpoint_create_called);
  ASSERT_EQ(2, cras_bt_player_create_called);
}

}  // namespace

extern "C" {
dbus_bool_t dbus_connection_send_with_reply(DBusConnection* connection,
                                            DBusMessage* message,
                                            DBusPendingCall** pending_return,
                                            int timeout_milliseconds) {
  return true;
}
DBusMessage* dbus_connection_send_with_reply_and_block(
    DBusConnection* connection,
    DBusMessage* message,
    int timeout_milliseconds,
    DBusError* error) {
  return NULL;
}

dbus_bool_t dbus_connection_add_filter(DBusConnection* connection,
                                       DBusHandleMessageFunction function,
                                       void* user_data,
                                       DBusFreeFunction free_data_function) {
  dbus_connection_add_filter_called++;
  return true;
}
dbus_bool_t dbus_connection_send(DBusConnection* connection,
                                 DBusMessage* message,
                                 dbus_uint32_t* sefial) {
  return true;
}
void dbus_connection_remove_filter(DBusConnection* connection,
                                   DBusHandleMessageFunction function,
                                   void* user_data) {
  dbus_connection_remove_filter_called++;
}

struct cras_bt_adapter* cras_bt_adapter_create(DBusConnection* conn,
                                               const char* object_path) {
  return NULL;
}
void cras_bt_adapter_reset() {}
struct cras_bt_adapter* cras_bt_adapter_get(const char* object_path) {
  return NULL;
}
const char* cras_bt_adapter_address(const struct cras_bt_adapter* adapter) {
  return "12:34:56:78:90:ab";
}
void cras_bt_adapter_destroy(struct cras_bt_adapter* adapter) {}
void cras_bt_adapter_update_properties(
    struct cras_bt_adapter* adapter,
    DBusMessageIter* properties_array_iter,
    DBusMessageIter* invalidated_array_iter) {}

struct cras_bt_device* cras_bt_device_create(DBusConnection* conn,
                                             const char* object_path) {
  return NULL;
}
void cras_bt_device_reset() {}
struct cras_bt_device* cras_bt_device_get(const char* object_path) {
  return NULL;
}
const char* cras_bt_device_address(const struct cras_bt_device* device) {
  return "11:22:33:44:55:66";
}
void cras_bt_device_remove(struct cras_bt_device* device) {}
void cras_bt_device_update_properties(struct cras_bt_device* device,
                                      DBusMessageIter* properties_array_iter,
                                      DBusMessageIter* invalidated_array_iter) {
}
void cras_bt_policy_start() {
  cras_bt_policy_start_called++;
};
void cras_bt_policy_stop() {
  cras_bt_policy_stop_called++;
};
int cras_hfp_ag_profile_create(DBusConnection* conn) {
  cras_hfp_ag_profile_create_called++;
  return 0;
}
int cras_hfp_ag_profile_destroy(DBusConnection* conn) {
  cras_hfp_ag_profile_destroy_called++;
  return 0;
}
void cras_telephony_start(DBusConnection* conn) {
  cras_telephony_start_called++;
}
void cras_telephony_stop() {
  cras_telephony_stop_called++;
}

int cras_a2dp_endpoint_create(DBusConnection* conn) {
  cras_a2dp_endpoint_create_called++;
  return 0;
}
int cras_a2dp_endpoint_destroy(DBusConnection* conn) {
  cras_a2dp_endpoint_destroy_called++;
  return 0;
}

int cras_bt_register_endpoints(DBusConnection* conn,
                               const struct cras_bt_adapter* adapter) {
  return 0;
}
void cras_bt_endpoint_reset() {}

struct cras_bt_transport* cras_bt_transport_create(DBusConnection* conn,
                                                   const char* object_path) {
  return NULL;
}
void cras_bt_transport_reset() {}

int cras_bt_register_profiles(DBusConnection* conn) {
  return 0;
}
void cras_bt_profile_reset() {}

int cras_bt_register_battery_provider(DBusConnection* conn,
                                      const struct cras_bt_adapter* adapter) {
  return 0;
}
void cras_bt_unregister_battery_provider(DBusConnection* conn) {
  cras_bt_unregister_battery_provider_called++;
}
void cras_bt_battery_provider_reset() {}

int cras_bt_register_player(DBusConnection* conn,
                            const struct cras_bt_adapter* adapter) {
  return 0;
}
int cras_bt_player_create(DBusConnection* conn) {
  cras_bt_player_create_called++;
  return 0;
}
int cras_bt_player_destroy(DBusConnection* conn) {
  cras_bt_player_destroy_called++;
  return 0;
}
struct cras_bt_transport* cras_bt_transport_get(const char* object_path) {
  return NULL;
}
const char* cras_bt_transport_object_path(
    const struct cras_bt_transport* transport) {
  return "/obj/path/transport";
}
void cras_bt_transport_remove(struct cras_bt_transport* transport) {}
void cras_bt_transport_update_properties(
    struct cras_bt_transport* transport,
    DBusMessageIter* properties_array_iter,
    DBusMessageIter* invalidated_array_iter) {}
}

// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <dbus/dbus.h>
#include <gtest/gtest.h>

#include "dbus_test.h"

extern "C" {
#include "cras_bluetooth.h"
}

namespace {

class BluetoothTestSuite : public DBusTest {
  virtual void SetUp() {
    DBusTest::SetUp();

    ExpectMethodCall("", DBUS_INTERFACE_DBUS, "AddMatch")
        .SendReplyNoWait();
  }

  virtual void TearDown() {
    ExpectMethodCall("", DBUS_INTERFACE_DBUS, "RemoveMatch")
        .SendReplyNoWait();

    cras_bluetooth_stop(conn_);

    DBusTest::TearDown();
  }
};


/* Verify that when BlueZ is running and a default adapter is present,
 * the adapter object path is set.
 */
TEST_F(BluetoothTestSuite, AdapterPresent) {
  const char fake_adapter_path[] = "/org/fake/hci0";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendReply()
      .WithObjectPath(fake_adapter_path);

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const char *adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_STREQ(fake_adapter_path, adapter_path);
}

/* Verify that when BlueZ is running but a default adapter is not present,
 * the adapter object path remains NULL.
 */
TEST_F(BluetoothTestSuite, AdapterNotPresent) {
  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendError("org.bluez.Error.NoSuchAdapter", "No such adapter");

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const char *adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_TRUE(adapter_path == NULL);
}

/* Verify that when BlueZ is not running the adapter object path
 * remains NULL.
 */
TEST_F(BluetoothTestSuite, BluezNotPresent) {
  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendError("org.freedesktop.DBus.Error.ServiceUnknown",
                 "No such service");

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const char *adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_TRUE(adapter_path == NULL);
}

/* Verify that a signal from BlueZ to change the default adapter results
 * in the adapter object path being changed.
 */
TEST_F(BluetoothTestSuite, AdapterChanged) {
  const char fake_adapter_path1[] = "/org/fake/hci0";
  const char fake_adapter_path2[] = "/org/fake/hci1";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendReply()
      .WithObjectPath(fake_adapter_path1);

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const char *adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_STREQ(fake_adapter_path1, adapter_path);

  CreateSignal("/", "org.bluez.Manager", "DefaultAdapterChanged")
      .WithObjectPath(fake_adapter_path2)
      .Send();

  WaitForMatches();

  adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_STREQ(fake_adapter_path2, adapter_path);
}

/* Verify that a signal from BlueZ to change the default adapter, when
 * the default adapter was not initially present, results in the adapter
 * object path being set.
 */
TEST_F(BluetoothTestSuite, AdapterChangedToPresent) {
  const char fake_adapter_path[] = "/org/fake/hci0";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendError("org.bluez.Error.NoSuchAdapter", "No such adapter");

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const char *adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_TRUE(adapter_path == NULL);

  CreateSignal("/", "org.bluez.Manager", "DefaultAdapterChanged")
      .WithObjectPath(fake_adapter_path)
      .Send();

  WaitForMatches();

  adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_STREQ(fake_adapter_path, adapter_path);
}

/* Verify that a signal from BlueZ to remove the default adapter results
 * in the adapter object path being cleared.
 */
TEST_F(BluetoothTestSuite, AdapterRemoved) {
  const char fake_adapter_path[] = "/org/fake/hci0";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendReply()
      .WithObjectPath(fake_adapter_path);

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const char *adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_STREQ(fake_adapter_path, adapter_path);

  CreateSignal("/", "org.bluez.Manager", "AdapterRemoved")
      .WithObjectPath(fake_adapter_path)
      .Send();

  WaitForMatches();

  adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_TRUE(adapter_path == NULL);
}

/* Verify that a signal from D-Bus when BlueZ starts results in the default
 * adapter being obtained and set.
 */
TEST_F(BluetoothTestSuite, BluezStarts) {
  const char fake_adapter_path[] = "/org/fake/hci0";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendError("org.freedesktop.DBus.Error.ServiceUnknown",
                 "No such service");

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const char *adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_TRUE(adapter_path == NULL);

  CreateSignal("/", "org.freedesktop.DBus", "NameOwnerChanged")
      .WithString("org.bluez")
      .WithString("")
      .WithString(":1.100")
      .Send();

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendReply()
      .WithObjectPath(fake_adapter_path);

  WaitForMatches();

  adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_STREQ(fake_adapter_path, adapter_path);
}

/* Verify that a signal from D-Bus when BlueZ stops results in the default
 * adapter being cleared.
 */
TEST_F(BluetoothTestSuite, BluezStops) {
  const char fake_adapter_path[] = "/org/fake/hci0";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendReply()
      .WithObjectPath(fake_adapter_path);

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const char *adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_STREQ(fake_adapter_path, adapter_path);

  CreateSignal("/", "org.freedesktop.DBus", "NameOwnerChanged")
      .WithString("org.bluez")
      .WithString(":1.100")
      .WithString("")
      .Send();

  WaitForMatches();

  adapter_path = cras_bluetooth_adapter_object_path();
  EXPECT_TRUE(adapter_path == NULL);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

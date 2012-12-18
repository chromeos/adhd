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

    cras_bluetooth_stop();

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


/* Verify that we fetch the list of devices from the default adapter. */
TEST_F(BluetoothTestSuite, AdapterWithDevices) {
  const char fake_adapter_path[] = "/org/fake/hci0";
  const char fake_device_path[] = "/org/fake/hci0/dev_04_08_15_16_23_42";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendReply()
      .WithObjectPath(fake_adapter_path);

  std::vector<std::string> devices;
  devices.push_back(fake_device_path);

  ExpectMethodCall(fake_adapter_path, "org.bluez.Adapter", "GetProperties")
      .SendReply()
      .AsPropertyDictionary()
      .WithString("Devices")
      .WithArrayOfObjectPaths(devices);

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const struct cras_bluetooth_device *device =
      cras_bluetooth_adapter_first_device();
  EXPECT_FALSE(device == NULL);

  const char *device_path = cras_bluetooth_device_object_path(device);
  EXPECT_STREQ(fake_device_path, device_path);

  device = cras_bluetooth_adapter_next_device(device);
  EXPECT_TRUE(device == NULL);
}

/* Verify that an adapter with multiple devices fetches all of them. */
TEST_F(BluetoothTestSuite, AdapterWithMultipleDevices) {
  const char fake_adapter_path[] = "/org/fake/hci0";
  const char fake_device_path1[] = "/org/fake/hci0/dev_04_08_15_16_23_42";
  const char fake_device_path2[] = "/org/fake/hci0/dev_01_1A_2B_1B_2B_03";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendReply()
      .WithObjectPath(fake_adapter_path);

  std::vector<std::string> devices;
  devices.push_back(fake_device_path1);
  devices.push_back(fake_device_path2);

  ExpectMethodCall(fake_adapter_path, "org.bluez.Adapter", "GetProperties")
      .SendReply()
      .AsPropertyDictionary()
      .WithString("Devices")
      .WithArrayOfObjectPaths(devices);

  cras_bluetooth_start(conn_);

  WaitForMatches();

  int found1 = 0, found2 = 0, found_other = 0;
  for (const struct cras_bluetooth_device *device =
           cras_bluetooth_adapter_first_device(); device != NULL;
       device = cras_bluetooth_adapter_next_device(device)) {
    const char *device_path = cras_bluetooth_device_object_path(device);
    if (strcmp(fake_device_path1, device_path) == 0)
      ++found1;
    else if (strcmp(fake_device_path2, device_path) == 0)
      ++found2;
    else
      ++found_other;
  }

  EXPECT_EQ(1, found1);
  EXPECT_EQ(1, found2);
  EXPECT_EQ(0, found_other);
}

/* Verify that the list of device is cleared when the adapter is removed. */
TEST_F(BluetoothTestSuite, AdapterWithDevicesRemoved) {
  const char fake_adapter_path[] = "/org/fake/hci0";
  const char fake_device_path[] = "/org/fake/hci0/dev_04_08_15_16_23_42";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendReply()
      .WithObjectPath(fake_adapter_path);

  std::vector<std::string> devices;
  devices.push_back(fake_device_path);

  ExpectMethodCall(fake_adapter_path, "org.bluez.Adapter", "GetProperties")
      .SendReply()
      .AsPropertyDictionary()
      .WithString("Devices")
      .WithArrayOfObjectPaths(devices);

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const struct cras_bluetooth_device *device =
      cras_bluetooth_adapter_first_device();
  EXPECT_FALSE(device == NULL);

  CreateSignal("/", "org.bluez.Manager", "AdapterRemoved")
      .WithObjectPath(fake_adapter_path)
      .Send();

  WaitForMatches();

  device = cras_bluetooth_adapter_first_device();
  EXPECT_TRUE(device == NULL);
}

/* Verify that when the default adapter changes, the list of devices is
 * changed too.
 */
TEST_F(BluetoothTestSuite, AdapterWithDevicesChanged) {
  const char fake_adapter_path1[] = "/org/fake/hci0";
  const char fake_adapter_path2[] = "/org/fake/hci1";
  const char fake_device_path1[] = "/org/fake/hci0/dev_04_08_15_16_23_42";
  const char fake_device_path2[] = "/org/fake/hci1/dev_01_1A_2B_1B_2B_03";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendReply()
      .WithObjectPath(fake_adapter_path1);

  std::vector<std::string> devices;
  devices.push_back(fake_device_path1);

  ExpectMethodCall(fake_adapter_path1, "org.bluez.Adapter", "GetProperties")
      .SendReply()
      .AsPropertyDictionary()
      .WithString("Devices")
      .WithArrayOfObjectPaths(devices);

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const struct cras_bluetooth_device *device =
      cras_bluetooth_adapter_first_device();
  EXPECT_FALSE(device == NULL);

  const char *device_path = cras_bluetooth_device_object_path(device);
  EXPECT_STREQ(fake_device_path1, device_path);

  device = cras_bluetooth_adapter_next_device(device);
  EXPECT_TRUE(device == NULL);

  devices.clear();
  devices.push_back(fake_device_path2);

  ExpectMethodCall(fake_adapter_path2, "org.bluez.Adapter", "GetProperties")
      .SendReply()
      .AsPropertyDictionary()
      .WithString("Devices")
      .WithArrayOfObjectPaths(devices);

  CreateSignal("/", "org.bluez.Manager", "DefaultAdapterChanged")
      .WithObjectPath(fake_adapter_path2)
      .Send();

  WaitForMatches();

  device = cras_bluetooth_adapter_first_device();
  EXPECT_FALSE(device == NULL);

  device_path = cras_bluetooth_device_object_path(device);
  EXPECT_STREQ(fake_device_path2, device_path);

  device = cras_bluetooth_adapter_next_device(device);
  EXPECT_TRUE(device == NULL);
}

/* Verify that we respond to the DeviceCreated signal and add the new
 * object path to the list of devices.
 */
TEST_F(BluetoothTestSuite, DeviceCreated) {
  const char fake_adapter_path[] = "/org/fake/hci0";
  const char fake_device_path1[] = "/org/fake/hci0/dev_04_08_15_16_23_42";
  const char fake_device_path2[] = "/org/fake/hci0/dev_01_1A_2B_1B_2B_03";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendReply()
      .WithObjectPath(fake_adapter_path);

  std::vector<std::string> devices;
  devices.push_back(fake_device_path1);

  ExpectMethodCall(fake_adapter_path, "org.bluez.Adapter", "GetProperties")
      .SendReply()
      .AsPropertyDictionary()
      .WithString("Devices")
      .WithArrayOfObjectPaths(devices);

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const struct cras_bluetooth_device *first_device =
      cras_bluetooth_adapter_first_device();
  EXPECT_FALSE(first_device == NULL);

  CreateSignal(fake_adapter_path, "org.bluez.Adapter", "DeviceCreated")
      .WithObjectPath(fake_device_path2)
      .Send();

  WaitForMatches();

  const struct cras_bluetooth_device *device =
      cras_bluetooth_adapter_first_device();
  if (device == first_device)
    device = cras_bluetooth_adapter_next_device(device);
  EXPECT_FALSE(device == NULL);

  const char *device_path = cras_bluetooth_device_object_path(device);
  EXPECT_STREQ(fake_device_path2, device_path);

  device = cras_bluetooth_adapter_next_device(device);
  if (device == first_device)
    device = cras_bluetooth_adapter_next_device(device);
  EXPECT_TRUE(device == NULL);
}

/* Verify that we respond to the DeviceRemoved signal and remove the given
 * object path from the list of devices.
 */
TEST_F(BluetoothTestSuite, DeviceRemoved) {
  const char fake_adapter_path[] = "/org/fake/hci0";
  const char fake_device_path1[] = "/org/fake/hci0/dev_04_08_15_16_23_42";
  const char fake_device_path2[] = "/org/fake/hci0/dev_01_1A_2B_1B_2B_03";

  ExpectMethodCall("/", "org.bluez.Manager", "DefaultAdapter")
      .SendReply()
      .WithObjectPath(fake_adapter_path);

  std::vector<std::string> devices;
  devices.push_back(fake_device_path1);
  devices.push_back(fake_device_path2);

  ExpectMethodCall(fake_adapter_path, "org.bluez.Adapter", "GetProperties")
      .SendReply()
      .AsPropertyDictionary()
      .WithString("Devices")
      .WithArrayOfObjectPaths(devices);

  cras_bluetooth_start(conn_);

  WaitForMatches();

  const struct cras_bluetooth_device *device =
      cras_bluetooth_adapter_first_device();
  EXPECT_FALSE(device == NULL);

  device = cras_bluetooth_adapter_next_device(device);
  EXPECT_FALSE(device == NULL);

  device = cras_bluetooth_adapter_next_device(device);
  EXPECT_TRUE(device == NULL);

  CreateSignal(fake_adapter_path, "org.bluez.Adapter", "DeviceRemoved")
      .WithObjectPath(fake_device_path1)
      .Send();

  WaitForMatches();

  device = cras_bluetooth_adapter_first_device();
  EXPECT_FALSE(device == NULL);

  const char *device_path = cras_bluetooth_device_object_path(device);
  EXPECT_STREQ(fake_device_path2, device_path);

  device = cras_bluetooth_adapter_next_device(device);
  EXPECT_TRUE(device == NULL);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

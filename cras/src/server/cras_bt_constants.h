/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_CONSTANTS_H_
#define CRAS_BT_CONSTANTS_H_

#define BLUEZ_SERVICE "org.bluez"

#define BLUEZ_INTERFACE_ADAPTER "org.bluez.Adapter1"
#define BLUEZ_INTERFACE_DEVICE "org.bluez.Device1"
#define BLUEZ_INTERFACE_MEDIA "org.bluez.Media1"
#define BLUEZ_INTERFACE_MEDIA_ENDPOINT "org.bluez.MediaEndpoint1"
#define BLUEZ_INTERFACE_MEDIA_PLAYER "org.mpris.MediaPlayer2.Player"
#define BLUEZ_INTERFACE_MEDIA_TRANSPORT "org.bluez.MediaTransport1"
#define BLUEZ_INTERFACE_PLAYER "org.bluez.MediaPlayer1"
#define BLUEZ_INTERFACE_PROFILE "org.bluez.Profile1"
#define BLUEZ_PROFILE_MGMT_INTERFACE "org.bluez.ProfileManager1"
/* Remove once our D-Bus header files are updated to define this. */
#ifndef DBUS_INTERFACE_OBJECT_MANAGER
#define DBUS_INTERFACE_OBJECT_MANAGER "org.freedesktop.DBus.ObjectManager"
#endif

/* UUIDs taken from lib/uuid.h in the BlueZ source */
#define HSP_HS_UUID "00001108-0000-1000-8000-00805f9b34fb"
#define HSP_AG_UUID "00001112-0000-1000-8000-00805f9b34fb"

#define HFP_HF_UUID "0000111e-0000-1000-8000-00805f9b34fb"
#define HFP_AG_UUID "0000111f-0000-1000-8000-00805f9b34fb"

#define A2DP_SOURCE_UUID "0000110a-0000-1000-8000-00805f9b34fb"
#define A2DP_SINK_UUID "0000110b-0000-1000-8000-00805f9b34fb"

#define AVRCP_REMOTE_UUID "0000110e-0000-1000-8000-00805f9b34fb"
#define AVRCP_TARGET_UUID "0000110c-0000-1000-8000-00805f9b34fb"

#define GENERIC_AUDIO_UUID "00001203-0000-1000-8000-00805f9b34fb"

/* Constants for CRAS BT player */
#define CRAS_DEFAULT_PLAYER "/org/chromium/Cras/Bluetooth/DefaultPlayer"
/* The longest possible player playback status is "forward-seek" */
#define CRAS_PLAYER_PLAYBACK_STATUS_SIZE_MAX 13 * sizeof(char)
#define CRAS_PLAYER_PLAYBACK_STATUS_DEFAULT "playing"
/* Neither BlueZ or the MRPIS specs limited the player identity max size, 128
 * should be large enough for most.
 */
#define CRAS_PLAYER_IDENTITY_SIZE_MAX 128 * sizeof(char)
#define CRAS_PLAYER_IDENTITY_DEFAULT "DefaultPlayer"
#define CRAS_PLAYER_METADATA_SIZE_MAX 128 * sizeof(char)

#endif /* CRAS_BT_CONSTANTS_H_ */

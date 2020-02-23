/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_PLAYER_H_
#define CRAS_BT_PLAYER_H_

#include <dbus/dbus.h>
#include <stdbool.h>

#include "cras_bt_adapter.h"

/* Object to register as media player so that bluetoothd will report hardware
 * volume from device through bt_transport. Properties of the player are defined
 * in BlueZ's media API.
 */
struct cras_bt_player {
	const char *object_path;
	char *playback_status;
	char *identity;
	const char *loop_status;
	int64_t position;
	bool can_go_next;
	bool can_go_prev;
	bool can_play;
	bool can_pause;
	bool can_control;
	bool shuffle;
	void (*message_cb)(const char *message);
};

/* Creates a player object and register it to bluetoothd.
 * Args:
 *    conn - The dbus connection.
 */
int cras_bt_player_create(DBusConnection *conn);

/* Registers created player to bluetoothd. This is used when an bluetooth
 * adapter got enumerated.
 * Args:
 *    conn - The dbus connection.
 *    adapter - The enumerated bluetooth adapter.
 */
int cras_bt_register_player(DBusConnection *conn,
			    const struct cras_bt_adapter *adapter);

/* Updates playback status for player and notifies bluetoothd
 * Args:
 *    conn - The dbus connection.
 *    status - The player playback status.
 */
int cras_bt_player_update_playback_status(DBusConnection *conn,
					  const char *status);

/* Updates the player identity and notifies bluetoothd.
 * Args:
 *    conn - The dbus connection.
 *    identity - The identity of the registered player. This could be the name
 *               of the app or the name of the site playing
 *media.
 */
int cras_bt_player_update_identity(DBusConnection *conn, const char *identity);

/* Updates the player current track's position and notifies bluetoothd.
 * Args:
 *    conn - The dbus connection.
 *    position - The current track position in microseconds.
 */
int cras_bt_player_update_position(DBusConnection *conn,
				   const dbus_int64_t position);
#endif /* CRAS_BT_PLAYER_H_ */

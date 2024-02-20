/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_BT_PLAYER_H_
#define CRAS_SRC_SERVER_CRAS_BT_PLAYER_H_

#include <dbus/dbus.h>
#include <stdbool.h>
#include <stdint.h>

#include "cras/src/server/cras_bt_adapter.h"
#include "cras/src/server/cras_bt_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates a player object and register it to bluetoothd.
 * Args:
 *    conn - The dbus connection.
 */
int cras_bt_player_create(DBusConnection* conn);

// Unregisters player callback from dBus.
int cras_bt_player_destroy(DBusConnection* conn);

/* Registers created player to bluetoothd. This is used when an bluetooth
 * adapter got enumerated.
 * Args:
 *    conn - The dbus connection.
 *    adapter - The enumerated bluetooth adapter.
 */
int cras_bt_register_player(DBusConnection* conn,
                            const struct cras_bt_adapter* adapter);

/* Unregisters the created adapter from bluetoothd. This is used when CRAS
 * disconnects from bluetoothd.
 */
int cras_bt_unregister_player(DBusConnection* conn,
                              const struct cras_bt_adapter* adapter);

/* Updates playback status for player and notifies bluetoothd
 * Args:
 *    conn - The dbus connection.
 *    status - The player playback status.
 */
int cras_bt_player_update_playback_status(DBusConnection* conn,
                                          const char* status);

/* Updates the player identity and notifies bluetoothd.
 * Args:
 *    conn - The dbus connection.
 *    identity - The identity of the registered player. This could be the name
 *               of the app or the name of the site playing media.
 */
int cras_bt_player_update_identity(DBusConnection* conn, const char* identity);

/* Updates the player current track's position and notifies bluetoothd.
 * Args:
 *    conn - The dbus connection.
 *    position - The current track position in microseconds.
 */
int cras_bt_player_update_position(DBusConnection* conn,
                                   const dbus_int64_t position);

/* Updates the player current metadata and notifies bluetoothd.
 * Args:
 *    conn - The dbus connection.
 *    title - The title associated to the current media session.
 *    artist - The artist associated to the current media session.
 *    album - The album associated to the current media session.
 *    length - The duration in microseconds associated to the current media
 *             session.
 */
int cras_bt_player_update_metadata(DBusConnection* conn,
                                   const char* title,
                                   const char* artist,
                                   const char* album,
                                   const dbus_int64_t length);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_BT_PLAYER_H_

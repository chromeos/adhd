/* Copyright (c) 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_TRANSPORT_H_
#define CRAS_BT_TRANSPORT_H_

#include <dbus/dbus.h>
#include <stdint.h>

#include "cras_bt_device.h"

struct cras_bt_transport;

enum cras_bt_transport_state {
	CRAS_BT_TRANSPORT_STATE_DISCONNECTED,
	CRAS_BT_TRANSPORT_STATE_CONNECTING,
	CRAS_BT_TRANSPORT_STATE_CONNECTED,
	CRAS_BT_TRANSPORT_STATE_PLAYING
};


struct cras_bt_transport *cras_bt_transport_create(DBusConnection *conn,
						   const char *object_path);
void cras_bt_transport_destroy(struct cras_bt_transport *transport);
void cras_bt_transport_reset();

struct cras_bt_transport *cras_bt_transport_get(const char *object_path);
size_t cras_bt_transport_get_list(
	struct cras_bt_transport ***transport_list_out);

const char *cras_bt_transport_object_path(
	const struct cras_bt_transport *transport);
struct cras_bt_device *cras_bt_transport_device(
	const struct cras_bt_transport *transport);
enum cras_bt_device_profile cras_bt_transport_profile(
	const struct cras_bt_transport *transport);
int cras_bt_transport_codec(const struct cras_bt_transport *transport);
int cras_bt_transport_configuration(const struct cras_bt_transport *transport,
				    void *configuration, int len);
enum cras_bt_transport_state cras_bt_transport_state(
	const struct cras_bt_transport *transport);

int cras_bt_transport_fd(const struct cras_bt_transport *transport);
uint16_t cras_bt_transport_read_mtu(const struct cras_bt_transport *transport);
uint16_t cras_bt_transport_write_mtu(const struct cras_bt_transport *transport);

void cras_bt_transport_update_properties(
	struct cras_bt_transport *transport,
	DBusMessageIter *properties_array_iter,
	DBusMessageIter *invalidated_array_iter);

int cras_bt_transport_acquire(struct cras_bt_transport *transport);
int cras_bt_transport_release(struct cras_bt_transport *transport);

#endif /* CRAS_BT_TRANSPORT_H_ */

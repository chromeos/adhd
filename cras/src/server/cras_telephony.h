/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_TELEPHONY_H_
#define CRAS_TELEPHONY_H_

#include <dbus/dbus.h>

void cras_telephony_start(DBusConnection *conn);

void cras_telephony_stop();

#endif /* CRAS_TELEPHONY_H_ */

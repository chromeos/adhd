/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_HFP_AG_PROFILE_H_
#define CRAS_HFP_AG_PROFILE_H_

#include <dbus/dbus.h>

struct hfp_slc_handle;

/* Adds a profile instance for HFP AG (Hands-Free Profile Audio Gateway). */
int cras_hfp_ag_profile_create(DBusConnection *conn);


/* Adds a profile instance for HSP AG (Headset Profile Audio Gateway). */
int cras_hsp_ag_profile_create(DBusConnection *conn);

/* Gets the active SLC handle. Used for HFP qualification. */
struct hfp_slc_handle *cras_hfp_ag_get_active_handle();

#endif /* CRAS_HFP_AG_PROFILE_H_ */

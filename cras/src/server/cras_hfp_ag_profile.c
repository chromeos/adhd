/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <unistd.h>

#include "cras_bt_constants.h"
#include "cras_bt_profile.h"
#include "cras_hfp_ag_profile.h"

#define HFP_AG_PROFILE_NAME "Headset Gateway"
#define HFP_AG_PROFILE_PATH "/org/chromium/Cras/Bluetooth/HFPAG"
#define HFP_VERSION_1_5 0x0105


static void cras_hfp_ag_release(struct cras_bt_profile *profile)
{
}

static void cras_hfp_ag_new_connection(struct cras_bt_profile *profile,
				       struct cras_bt_transport *transport)
{
	int fd;
	cras_bt_transport_configuration(transport, &fd, sizeof(fd));
	close(fd);
}

static void cras_hfp_ag_request_disconnection(struct cras_bt_profile *profile,
		struct cras_bt_transport *transport)
{
}

static void cras_hfp_ag_cancel(struct cras_bt_profile *profile)
{
}

static struct cras_bt_profile cras_hfp_ag_profile = {
	.name = HFP_AG_PROFILE_NAME,
	.object_path = HFP_AG_PROFILE_PATH,
	.uuid = HFP_AG_UUID,
	.version = HFP_VERSION_1_5,
	.role = NULL,
	.features = 0,
	.release = cras_hfp_ag_release,
	.new_connection = cras_hfp_ag_new_connection,
	.request_disconnection = cras_hfp_ag_request_disconnection,
	.cancel = cras_hfp_ag_cancel
};

int cras_hfp_ag_profile_create(DBusConnection *conn)
{
	return cras_bt_add_profile(conn, &cras_hfp_ag_profile);
}

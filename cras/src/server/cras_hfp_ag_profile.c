/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <syslog.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cras_bt_adapter.h"
#include "cras_bt_constants.h"
#include "cras_bt_profile.h"
#include "cras_hfp_ag_profile.h"
#include "cras_hfp_info.h"
#include "cras_hfp_iodev.h"
#include "cras_hfp_slc.h"

#define HFP_AG_PROFILE_NAME "Headset Gateway"
#define HFP_AG_PROFILE_PATH "/org/chromium/Cras/Bluetooth/HFPAG"
#define HFP_VERSION_1_6 0x0106
#define HSP_AG_PROFILE_NAME "Headset Gateway"
#define HSP_AG_PROFILE_PATH "/org/chromium/Cras/Bluetooth/HSPAG"
#define HSP_VERSION_1_2 0x0102

#define HSP_AG_RECORD 							\
	"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>"			\
	"<record>"							\
	"  <attribute id=\"0x0001\">"					\
	"    <sequence>"						\
	"      <uuid value=\"" HSP_AG_UUID "\" />"			\
	"      <uuid value=\"" GENERIC_AUDIO_UUID "\" />"		\
	"    </sequence>"						\
	"  </attribute>"						\
	"  <attribute id=\"0x0004\">"					\
	"    <sequence>"						\
	"      <sequence>"						\
	"        <uuid value=\"0x0100\" />"				\
	"      </sequence>"						\
	"      <sequence>"						\
	"        <uuid value=\"0x0003\" />"				\
	"        <uint8 value=\"0x0c\" />"				\
	"      </sequence>"						\
	"    </sequence>"						\
	"  </attribute>"						\
	"  <attribute id=\"0x0005\">"					\
	"    <sequence>"						\
	"      <uuid value=\"0x1002\" />"				\
	"    </sequence>"						\
	"  </attribute>"						\
	"  <attribute id=\"0x0009\">"					\
	"    <sequence>"						\
	"      <sequence>"						\
	"        <uuid value=\"" HSP_HS_UUID "\" />"			\
	"        <uint16 value=\"0x0102\" />"				\
	"      </sequence>"						\
	"    </sequence>"						\
	"  </attribute>"						\
	"  <attribute id=\"0x0100\">"					\
	"    <text value=\"Voice Gateway\" />"				\
	"  </attribute>"						\
	"  <attribute id=\"0x0301\" >"					\
	"    <uint8 value=\"0x01\" />"					\
	"  </attribute>"						\
	"</record>"

static struct cras_iodev *idev;
static struct cras_iodev *odev;
static struct hfp_info *info;
static struct hfp_slc_handle *slc_handle;

static void destroy_hfp_resources()
{
	if (idev) {
		hfp_iodev_destroy(idev);
		idev = NULL;
	}
	if (odev) {
		hfp_iodev_destroy(odev);
		odev = NULL;
	}
	if (info) {
		hfp_info_destroy(info);
		info = NULL;
	}
	if (slc_handle) {
		hfp_slc_destroy(slc_handle);
		slc_handle = NULL;
	}
}

static void cras_hfp_ag_release(struct cras_bt_profile *profile)
{
	destroy_hfp_resources();
}

static int cras_hfp_ag_slc_initialized(struct hfp_slc_handle *handle, void *data)
{
	int fd;
	struct cras_bt_transport *transport = (struct cras_bt_transport *)data;

	info = hfp_info_create();
	idev = hfp_iodev_create(CRAS_STREAM_INPUT, transport, info);
	odev = hfp_iodev_create(CRAS_STREAM_OUTPUT, transport, info);

	if (!idev && !odev) {
		if (info)
			hfp_info_destroy(info);
		cras_bt_transport_configuration(transport, &fd, sizeof(fd));
		close(fd);
        }

	return 0;
}

static int cras_hfp_ag_slc_disconnected(struct hfp_slc_handle *handle)
{
	destroy_hfp_resources();
	return 0;
}

static void cras_hfp_ag_new_connection(struct cras_bt_profile *profile,
				       struct cras_bt_transport *transport)
{
	int fd;
	cras_bt_transport_configuration(transport, &fd, sizeof(fd));

	/* Destroy all existing devices and replace with new ones */
	destroy_hfp_resources();

	slc_handle = hfp_slc_create(fd,
				    0,
				    cras_hfp_ag_slc_initialized,
				    transport,
				    cras_hfp_ag_slc_disconnected);
}

static void cras_hfp_ag_request_disconnection(struct cras_bt_profile *profile,
		struct cras_bt_transport *transport)
{
	int fd;

	/* There is at most one device connected, just release it. */
	cras_hfp_ag_release(profile);
	cras_bt_transport_configuration(transport, &fd, sizeof(fd));
	close(fd);
}

static void cras_hfp_ag_cancel(struct cras_bt_profile *profile)
{
}

static struct cras_bt_profile cras_hfp_ag_profile = {
	.name = HFP_AG_PROFILE_NAME,
	.object_path = HFP_AG_PROFILE_PATH,
	.uuid = HFP_AG_UUID,
	.version = HFP_VERSION_1_6,
	.role = NULL,
	.features = 0,
	.record = NULL,
	.release = cras_hfp_ag_release,
	.new_connection = cras_hfp_ag_new_connection,
	.request_disconnection = cras_hfp_ag_request_disconnection,
	.cancel = cras_hfp_ag_cancel
};

int cras_hfp_ag_profile_create(DBusConnection *conn)
{
	return cras_bt_add_profile(conn, &cras_hfp_ag_profile);
}

static void cras_hsp_ag_new_connection(struct cras_bt_profile *profile,
				       struct cras_bt_transport *transport)
{
	int fd = 0;
	cras_bt_transport_configuration(transport, &fd, sizeof(fd));

	/* Destroy all existing devices and replace with new ones */
	destroy_hfp_resources();
	slc_handle = hfp_slc_create(fd, 1, NULL, transport,
				    cras_hfp_ag_slc_disconnected);

	cras_hfp_ag_slc_initialized(slc_handle, transport);
}

static struct cras_bt_profile cras_hsp_ag_profile = {
	.name = HSP_AG_PROFILE_NAME,
	.object_path = HSP_AG_PROFILE_PATH,
	.uuid = HSP_AG_UUID,
	.version = HSP_VERSION_1_2,
	.role = NULL,
	.record = HSP_AG_RECORD,
	.features = 0,
	.release = cras_hfp_ag_release,
	.new_connection = cras_hsp_ag_new_connection,
	.request_disconnection = cras_hfp_ag_request_disconnection,
	.cancel = cras_hfp_ag_cancel
};

int cras_hsp_ag_profile_create(DBusConnection *conn)
{
	return cras_bt_add_profile(conn, &cras_hsp_ag_profile);
}

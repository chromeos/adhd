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
#include "utlist.h"

#define STR(s) #s
#define VSTR(id) STR(id)

#define HFP_AG_PROFILE_NAME "Hands-Free Voice gateway"
#define HFP_AG_PROFILE_PATH "/org/chromium/Cras/Bluetooth/HFPAG"
#define HFP_VERSION_1_6 0x0106
#define HSP_AG_PROFILE_NAME "Headset Voice gateway"
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
	"        <uint16 value=\"" VSTR(HSP_VERSION_1_2) "\" />"	\
	"      </sequence>"						\
	"    </sequence>"						\
	"  </attribute>"						\
	"  <attribute id=\"0x0100\">"					\
	"    <text value=\"" HSP_AG_PROFILE_NAME "\" />"		\
	"  </attribute>"						\
	"  <attribute id=\"0x0301\" >"					\
	"    <uint8 value=\"0x01\" />"					\
	"  </attribute>"						\
	"</record>"


struct audio_gateway {
	struct cras_iodev *idev;
	struct cras_iodev *odev;
	struct hfp_info *info;
	struct hfp_slc_handle *slc_handle;
	struct cras_bt_device *device;
	enum cras_bt_device_profile profile;
	struct audio_gateway *prev, *next;
};

static struct audio_gateway *connected_ags;

static void destroy_audio_gateway(struct audio_gateway *ag)
{
	if (ag->idev)
		hfp_iodev_destroy(ag->idev);
	if (ag->odev)
		hfp_iodev_destroy(ag->odev);
	if (ag->info)
		hfp_info_destroy(ag->info);
	if (ag->slc_handle)
		hfp_slc_destroy(ag->slc_handle);
	free(ag);
}

static void cras_hfp_ag_release(struct cras_bt_profile *profile)
{
	struct audio_gateway *ag;
	DL_FOREACH(connected_ags, ag) {
		DL_DELETE(connected_ags, ag);
		destroy_audio_gateway(ag);
	}
}

static int cras_hfp_ag_slc_initialized(struct hfp_slc_handle *handle)
{
	struct audio_gateway *ag;

	DL_SEARCH_SCALAR(connected_ags, ag, slc_handle, handle);
	if (!ag)
		return -EINVAL;

	ag->info = hfp_info_create();
	ag->idev = hfp_iodev_create(CRAS_STREAM_INPUT, ag->device,
				    ag->profile, ag->info);
	ag->odev = hfp_iodev_create(CRAS_STREAM_OUTPUT, ag->device,
				    ag->profile, ag->info);

	if (!ag->idev && !ag->odev) {
		destroy_audio_gateway(ag);
		return -ENOMEM;
        }

	return 0;
}

static int cras_hfp_ag_slc_disconnected(struct hfp_slc_handle *handle)
{
	struct audio_gateway *ag;

	DL_SEARCH_SCALAR(connected_ags, ag, slc_handle, handle);
	if (!ag)
		return -EINVAL;

	DL_DELETE(connected_ags, ag);
	destroy_audio_gateway(ag);
	return 0;
}

static void cras_hfp_ag_new_connection(struct cras_bt_profile *profile,
				       struct cras_bt_device *device,
				       int rfcomm_fd)
{
	struct audio_gateway *ag;
	DL_FOREACH(connected_ags, ag) {
		if (ag->slc_handle && ag->device == device)
			break;
	}

	/* Destroy all existing devices and replace with new ones */
	if (ag) {
		DL_DELETE(connected_ags, ag);
		destroy_audio_gateway(ag);
	}

	ag = (struct audio_gateway *)calloc(1, sizeof(*ag));
	ag->device = device;
	ag->profile = cras_bt_device_profile_from_uuid(profile->uuid);
	ag->slc_handle = hfp_slc_create(rfcomm_fd,
					0,
					cras_hfp_ag_slc_initialized,
					cras_hfp_ag_slc_disconnected);
	DL_APPEND(connected_ags, ag);
}

static void cras_hfp_ag_request_disconnection(struct cras_bt_profile *profile,
					      struct cras_bt_device *device)
{
	struct audio_gateway *ag;
	DL_FOREACH(connected_ags, ag) {
		if (ag->slc_handle && ag->device == device) {
			DL_DELETE(connected_ags, ag);
			destroy_audio_gateway(ag);
		}
	}
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
	.features = HFP_SUPPORTED_FEATURE & 0x1F,
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
				       struct cras_bt_device *device,
				       int rfcomm_fd)
{
	struct audio_gateway *ag;
	DL_FOREACH(connected_ags, ag) {
		if (ag->slc_handle && ag->device == device)
			break;
	}

	/* Destroy all existing devices and replace with new ones */
	if (ag)  {
		DL_DELETE(connected_ags, ag);
		destroy_audio_gateway(ag);
	}

	ag = (struct audio_gateway *)calloc(1, sizeof(*ag));
	ag->device = device;
	ag->profile = cras_bt_device_profile_from_uuid(profile->uuid);
	ag->slc_handle = hfp_slc_create(rfcomm_fd, 1, NULL,
					cras_hfp_ag_slc_disconnected);
	DL_APPEND(connected_ags, ag);
	cras_hfp_ag_slc_initialized(ag->slc_handle);
}

static struct cras_bt_profile cras_hsp_ag_profile = {
	.name = HSP_AG_PROFILE_NAME,
	.object_path = HSP_AG_PROFILE_PATH,
	.uuid = HSP_AG_UUID,
	.version = HSP_VERSION_1_2,
	.role = NULL,
	.record = HSP_AG_RECORD,
	.release = cras_hfp_ag_release,
	.new_connection = cras_hsp_ag_new_connection,
	.request_disconnection = cras_hfp_ag_request_disconnection,
	.cancel = cras_hfp_ag_cancel
};

struct hfp_slc_handle *cras_hfp_ag_get_active_handle()
{
	/* Returns the first handle for HFP qualification. In future we
	 * might want this to return the HFP device user is selected. */
	return connected_ags ? connected_ags->slc_handle : NULL;
}

struct hfp_slc_handle *cras_hfp_ag_get_slc(struct cras_bt_device *device)
{
	struct audio_gateway *ag;
	DL_FOREACH(connected_ags, ag) {
		if (ag->device == device)
			return ag->slc_handle;
	}
	return NULL;
}

int cras_hsp_ag_profile_create(DBusConnection *conn)
{
	return cras_bt_add_profile(conn, &cras_hsp_ag_profile);
}

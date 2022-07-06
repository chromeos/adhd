/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "cras_fl_media.h"
#include "cras_fl_media_adaptor.h"

int handle_on_bluetooth_device_added(struct fl_media *active_fm,
				     const char *addr, const char *name,
				     struct cras_fl_a2dp_codec_config *codecs,
				     int32_t hfp_cap, bool abs_vol_supported)
{
	int a2dp_avail = cras_floss_get_a2dp_enabled() && codecs != NULL;
	int hfp_avail = cras_floss_get_hfp_enabled() && hfp_cap;

	if (!a2dp_avail & !hfp_avail)
		return -EINVAL;

	if (!active_fm->bt_io_mgr) {
		active_fm->bt_io_mgr = bt_io_manager_create();
		if (!active_fm->bt_io_mgr)
			return -EINVAL;
	}

	if (a2dp_avail) {
		syslog(LOG_DEBUG, "A2DP device added.");
		if (active_fm->a2dp) {
			syslog(LOG_WARNING,
			       "Multiple A2DP devices added, remove the older");
			bt_io_manager_remove_iodev(
				active_fm->bt_io_mgr,
				cras_floss_a2dp_get_iodev(active_fm->a2dp));
			cras_floss_a2dp_destroy(active_fm->a2dp);
		}
		active_fm->a2dp =
			cras_floss_a2dp_create(active_fm, addr, name, codecs);

		if (active_fm->a2dp) {
			cras_floss_a2dp_set_support_absolute_volume(
				active_fm->a2dp, abs_vol_supported);
			bt_io_manager_append_iodev(
				active_fm->bt_io_mgr,
				cras_floss_a2dp_get_iodev(active_fm->a2dp),
				CRAS_BT_FLAG_A2DP);
		} else {
			syslog(LOG_WARNING,
			       "Failed to create the cras_a2dp_manager");
		}
	}

	if (hfp_avail) {
		syslog(LOG_DEBUG, "HFP device added with capability %d.",
		       hfp_cap);
		if (active_fm->hfp) {
			syslog(LOG_WARNING,
			       "Multiple HFP devices added, remove the older");
			floss_media_hfp_suspend(active_fm);
		}
		active_fm->hfp = cras_floss_hfp_create(active_fm, addr, name,
						       hfp_cap & FL_CODEC_MSBC);

		if (active_fm->hfp) {
			bt_io_manager_append_iodev(
				active_fm->bt_io_mgr,
				cras_floss_hfp_get_input_iodev(active_fm->hfp),
				CRAS_BT_FLAG_HFP);
			bt_io_manager_append_iodev(
				active_fm->bt_io_mgr,
				cras_floss_hfp_get_output_iodev(active_fm->hfp),
				CRAS_BT_FLAG_HFP);
		} else {
			syslog(LOG_WARNING,
			       "Failed to create the cras_hfp_manager");
		}
	}
	if (active_fm->a2dp != NULL || active_fm->hfp != NULL) {
		bt_io_manager_set_nodes_plugged(active_fm->bt_io_mgr, 1);
	}
	return 0;
}

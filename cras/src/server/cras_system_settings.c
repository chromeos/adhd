/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>
#include <syslog.h>

#include "cras_system_settings.h"
#include "cras_util.h"

static struct {
	size_t volume; /* Volume index from 0-100. */
	cras_system_volume_changed_cb callback;
} settings;

void cras_system_settings_init()
{
	settings.volume = CRAS_MAX_SYSTEM_VOLUME;
	settings.callback = NULL;
}

void cras_system_set_volume(size_t volume)
{
	if (volume > CRAS_MAX_SYSTEM_VOLUME)
		syslog(LOG_DEBUG, "system volume set out of range %zu", volume);

	settings.volume = min(volume, CRAS_MAX_SYSTEM_VOLUME);
	if (settings.callback != NULL)
		settings.callback(settings.volume);
}

size_t cras_system_get_volume()
{
	return settings.volume;
}

void cras_system_register_volume_changed_cb(cras_system_volume_changed_cb cb)
{
	settings.callback = cb;
}


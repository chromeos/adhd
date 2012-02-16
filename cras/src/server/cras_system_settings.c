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
	int mute; /* 0 = unmuted, 1 = muted. */
	cras_system_volume_changed_cb volume_callback;
	void *volume_callback_data;
	cras_system_mute_changed_cb mute_callback;
	void *mute_callback_data;
} settings;

void cras_system_settings_init()
{
	settings.volume = CRAS_MAX_SYSTEM_VOLUME;
	settings.mute = 0;
	settings.volume_callback = NULL;
	settings.volume_callback_data = NULL;
	settings.mute_callback = NULL;
	settings.mute_callback_data = NULL;
}

void cras_system_set_volume(size_t volume)
{
	if (volume > CRAS_MAX_SYSTEM_VOLUME)
		syslog(LOG_DEBUG, "system volume set out of range %zu", volume);

	settings.volume = min(volume, CRAS_MAX_SYSTEM_VOLUME);
	if (settings.volume_callback != NULL)
		settings.volume_callback(settings.volume,
					 settings.volume_callback_data);
}

size_t cras_system_get_volume()
{
	return settings.volume;
}

void cras_system_register_volume_changed_cb(cras_system_volume_changed_cb cb,
					    void *arg)
{
	settings.volume_callback = cb;
	settings.volume_callback_data = arg;
}

void cras_system_set_mute(int mute)
{
	settings.mute = !!mute;
	if (settings.mute_callback != NULL)
		settings.mute_callback(settings.mute,
					 settings.mute_callback_data);
}

int cras_system_get_mute()
{
	return settings.mute;
}

void cras_system_register_mute_changed_cb(cras_system_mute_changed_cb cb,
					  void *arg)
{
	settings.mute_callback = cb;
	settings.mute_callback_data = arg;
}


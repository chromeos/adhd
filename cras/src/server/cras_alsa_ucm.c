/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <alsa/use-case.h>
#include <syslog.h>

#include "cras_alsa_ucm.h"

static const char default_verb[] = "HiFi";
static const char jack_var[] = "JackName";
static const char edid_var[] = "EDIDFile";

static int device_enabled(snd_use_case_mgr_t *mgr, const char *dev)
{
	const char **list;
	unsigned int i;
	int num_devs;
	int enabled = 0;

	num_devs = snd_use_case_get_list(mgr, "_enadevs", &list);
	if (num_devs <= 0)
		return 0;

	for (i = 0; i < num_devs; i++)
		if (!strcmp(dev, list[i])) {
			enabled = 1;
			break;
		}

	snd_use_case_free_list(list, num_devs);
	return enabled;
}

/* Exported Interface */

snd_use_case_mgr_t *ucm_create(const char *name)
{
	snd_use_case_mgr_t *mgr;
	int rc;

	if (!name)
		return NULL;

	rc = snd_use_case_mgr_open(&mgr, name);
	if (rc)
		return NULL;

	rc = snd_use_case_set(mgr, "_verb", default_verb);
	if (rc) {
		ucm_destroy(mgr);
		return NULL;
	}

	return mgr;
}

void ucm_destroy(snd_use_case_mgr_t *mgr)
{
	snd_use_case_mgr_close(mgr);
}

int ucm_set_enabled(snd_use_case_mgr_t *mgr, const char *dev, int enable)
{
	if (device_enabled(mgr, dev) == !!enable)
		return 0;

	return snd_use_case_set(mgr, enable ? "_enadev" : "_disdev", dev);
}

char *ucm_get_dev_for_jack(snd_use_case_mgr_t *mgr, const char *jack)
{
	const char **list;
	char *dev_name = NULL;
	unsigned int i;
	int num_devs;
	int rc;

	num_devs = snd_use_case_get_list(mgr, "_devices/HiFi", &list);
	if (num_devs <= 0)
		return NULL;

	for (i = 0; i < num_devs; i++) {
		char *id;
		const char *this_jack;

		if (!list[i])
			continue;

		id = malloc(strlen(jack_var) +
			    strlen(list[i]) +
			    strlen(default_verb) + 4);
		if (!id)
			goto return_name;

		sprintf(id, "=%s/%s/%s", jack_var, list[i], default_verb);
		rc = snd_use_case_get(mgr, id, &this_jack);
		free(id);
		if (rc)
			continue;

		if (!strcmp(jack, this_jack)) {
			dev_name = strdup(list[i]);
			free((void *)this_jack);
			break;
		}
		free((void *)this_jack);
	}

return_name:
	snd_use_case_free_list(list, num_devs);
	return dev_name;
}

const char *ucm_get_edid_file_for_dev(snd_use_case_mgr_t *mgr, const char *dev)
{
	const char *file_name;
	char *id;
	int rc;

	id = malloc(strlen(edid_var) +
			strlen(dev) +
			strlen(default_verb) + 4);
	if (!id)
		return NULL;

	sprintf(id, "=%s/%s/%s", edid_var, dev, default_verb);
	rc = snd_use_case_get(mgr, id, &file_name);
	free(id);
	if (rc)
		return NULL;

	return file_name;
}

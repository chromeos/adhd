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
static const char cap_var[] = "CaptureControl";
static const char output_dsp_name_var[] = "OutputDspName";
static const char input_dsp_name_var[] = "InputDspName";

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

static int get_var(snd_use_case_mgr_t *mgr, const char *var, const char *dev,
		   const char *verb, const char **value)
{
	char *id;
	int rc;

	id = malloc(strlen(var) + strlen(dev) + strlen(verb) + 4);
	if (!id)
		return -ENOMEM;
	sprintf(id, "=%s/%s/%s", var, dev, verb);
	rc = snd_use_case_get(mgr, id, value);

	free((void *)id);
	return rc;
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

char *ucm_get_cap_control(snd_use_case_mgr_t *mgr, const char *ucm_dev)
{
	char *control_name = NULL;
	const char *value;
	int rc;

	rc = get_var(mgr, cap_var, ucm_dev, default_verb, &value);
	if (!rc) {
		control_name = strdup(value);
		free((void *)value);
	}

	return control_name;
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
		const char *this_jack;

		if (!list[i])
			continue;

		rc = get_var(mgr, jack_var, list[i], default_verb, &this_jack);
		if (!rc && !strcmp(jack, this_jack)) {
			dev_name = strdup(list[i]);
			free((void *)this_jack);
			break;
		}
		free((void *)this_jack);
	}

	snd_use_case_free_list(list, num_devs);
	return dev_name;
}

const char *ucm_get_edid_file_for_dev(snd_use_case_mgr_t *mgr, const char *dev)
{
	const char *file_name;
	int rc;

	rc = get_var(mgr, edid_var, dev, default_verb, &file_name);
	if (rc)
		return NULL;

	return file_name;
}

const char *ucm_get_dsp_name(snd_use_case_mgr_t *mgr, const char *ucm_dev,
			     int direction)
{
	const char *var = (direction == CRAS_STREAM_OUTPUT)
		? output_dsp_name_var
		: input_dsp_name_var;
	const char *dsp_name = NULL;
	int rc;

	rc = get_var(mgr, var, ucm_dev, default_verb, &dsp_name);
	if (rc)
		return NULL;

	return dsp_name;
}

const char *ucm_get_dsp_name_default(snd_use_case_mgr_t *mgr, int direction)
{
	return ucm_get_dsp_name(mgr, "", direction);
}

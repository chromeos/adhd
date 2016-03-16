/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <alsa/use-case.h>
#include <string.h>
#include <syslog.h>

#include "cras_alsa_ucm.h"
#include "utlist.h"

static const char default_verb[] = "HiFi";
static const char jack_var[] = "JackName";
static const char edid_var[] = "EDIDFile";
static const char cap_var[] = "CaptureControl";
static const char mic_positions[] = "MicPositions";
static const char override_type_name_var[] = "OverrideNodeType";
static const char output_dsp_name_var[] = "OutputDspName";
static const char input_dsp_name_var[] = "InputDspName";
static const char mixer_var[] = "MixerName";
static const char swap_mode_suffix[] = "Swap Mode";
static const char min_buffer_level_var[] = "MinBufferLevel";
static const char disable_software_volume[] = "DisableSoftwareVolume";
static const char playback_device_name_var[] = "PlaybackPCM";
static const char capture_device_name_var[] = "CapturePCM";
static const char coupled_mixers[] = "CoupledMixers";
/* Set this value in a SectionDevice to specify the maximum software gain in dBm
 * and enable software gain on this node. */
static const char max_software_gain[] = "MaxSoftwareGain";

static int device_enabled(snd_use_case_mgr_t *mgr, const char *dev)
{
	const char **list;
	unsigned int i;
	int num_devs;
	int enabled = 0;

	num_devs = snd_use_case_get_list(mgr, "_enadevs", &list);
	if (num_devs <= 0)
		return 0;

	for (i = 0; i < (unsigned int)num_devs; i++)
		if (!strcmp(dev, list[i])) {
			enabled = 1;
			break;
		}

	snd_use_case_free_list(list, num_devs);
	return enabled;
}

static int modifier_enabled(snd_use_case_mgr_t *mgr, const char *mod)
{
	const char **list;
	unsigned int mod_idx;
	int num_mods;

	num_mods = snd_use_case_get_list(mgr, "_enamods", &list);
	if (num_mods <= 0)
		return 0;

	for (mod_idx = 0; mod_idx < (unsigned int)num_mods; mod_idx++)
		if (!strcmp(mod, list[mod_idx]))
			break;

	snd_use_case_free_list(list, num_mods);
	return (mod_idx < (unsigned int)num_mods);
}

static int get_var(snd_use_case_mgr_t *mgr, const char *var, const char *dev,
		   const char *verb, const char **value)
{
	char *id;
	int rc;

	id = (char *)malloc(strlen(var) + strlen(dev) + strlen(verb) + 4);
	if (!id)
		return -ENOMEM;
	sprintf(id, "=%s/%s/%s", var, dev, verb);
	rc = snd_use_case_get(mgr, id, value);

	free((void *)id);
	return rc;
}

static int ucm_set_modifier_enabled(snd_use_case_mgr_t *mgr, const char *mod,
				    int enable)
{
	return snd_use_case_set(mgr, enable ? "_enamod" : "_dismod", mod);
}

static int ucm_str_ends_with_suffix(const char *str, const char *suffix)
{
	if (!str || !suffix)
		return 0;
	size_t len_str = strlen(str);
	size_t len_suffix = strlen(suffix);
	if (len_suffix > len_str)
		return 0;
	return strncmp(str + len_str - len_suffix, suffix, len_suffix) == 0;
}

static int ucm_section_exists_with_name(snd_use_case_mgr_t *mgr,
		const char *name, const char *identifier)
{
	const char **list;
	unsigned int i;
	int num_entries;
	int exist = 0;

	num_entries = snd_use_case_get_list(mgr, identifier, &list);
	if (num_entries <= 0)
		return 0;

	for (i = 0; i < (unsigned int)num_entries; i+=2) {

		if (!list[i])
			continue;

		if (strcmp(list[i], name) == 0) {
			exist = 1;
			break;
		}
	}
	snd_use_case_free_list(list, num_entries);
	return exist;
}

static int ucm_section_exists_with_suffix(snd_use_case_mgr_t *mgr,
		const char *suffix, const char *identifier)
{
	const char **list;
	unsigned int i;
	int num_entries;
	int exist = 0;

	num_entries = snd_use_case_get_list(mgr, identifier, &list);
	if (num_entries <= 0)
		return 0;

	for (i = 0; i < (unsigned int)num_entries; i+=2) {

		if (!list[i])
			continue;

		if (ucm_str_ends_with_suffix(list[i], suffix)) {
			exist = 1;
			break;
		}
	}
	snd_use_case_free_list(list, num_entries);
	return exist;
}

static int ucm_mod_exists_with_suffix(snd_use_case_mgr_t *mgr,
				const char *suffix)
{
	return ucm_section_exists_with_suffix(mgr, suffix, "_modifiers/HiFi");
}

static int ucm_mod_exists_with_name(snd_use_case_mgr_t *mgr, const char *name)
{
	return ucm_section_exists_with_name(mgr, name, "_modifiers/HiFi");
}

static char *ucm_get_section_for_var(snd_use_case_mgr_t *mgr, const char *var,
				     const char *value, const char *identifier,
				     enum CRAS_STREAM_DIRECTION direction)
{
	const char **list;
	char *section_name = NULL;
	unsigned int i;
	int num_entries;
	int rc;

	num_entries = snd_use_case_get_list(mgr, identifier, &list);
	if (num_entries <= 0)
		return NULL;

	/* snd_use_case_get_list fills list with pairs of device name and
	 * comment, so device names are in even-indexed elements. */
	for (i = 0; i < (unsigned int)num_entries; i+=2) {
		const char *this_value;

		if (!list[i])
			continue;

		/* Skip mic seciton for output, only check mic for input. */
		if (!strcmp(list[i], "Mic")) {
			if (direction == CRAS_STREAM_OUTPUT)
				continue;
		} else {
			if (direction == CRAS_STREAM_INPUT)
				continue;
		}

		rc = get_var(mgr, var, list[i], default_verb, &this_value);
		if (rc)
			continue;

		if (!strcmp(value, this_value)) {
			section_name = strdup(list[i]);
			free((void *)this_value);
			break;
		}
		free((void *)this_value);
	}

	snd_use_case_free_list(list, num_entries);
	return section_name;
}

static char *ucm_get_dev_for_var(snd_use_case_mgr_t *mgr, const char *var,
			  const char *value, enum CRAS_STREAM_DIRECTION dir) {
	return ucm_get_section_for_var(mgr, var, value, "_devices/HiFi", dir);
}

static const char *ucm_get_playback_device_name_for_dev(
	snd_use_case_mgr_t *mgr, const char *dev)
{
	const char *name = NULL;
	int rc;

	rc = get_var(mgr, playback_device_name_var, dev, default_verb, &name);
	if (rc)
		return NULL;

	return name;
}

static const char *ucm_get_capture_device_name_for_dev(
	snd_use_case_mgr_t *mgr, const char *dev)
{
	const char *name = NULL;
	int rc;

	rc = get_var(mgr, capture_device_name_var, dev, default_verb, &name);
	if (rc)
		return NULL;

	return name;
}

/* Get a list of mixer names specified in a UCM variable separated by ",".
 * E.g. "Left Playback,Right Playback".
 */
static struct mixer_name *ucm_get_mixer_names(snd_use_case_mgr_t *mgr,
		const char *dev, const char* var)
{
	const char *names_in_string = NULL;
	int rc;
	char *tokens, *name;
	struct mixer_name *names = NULL, *m_name;

	rc = get_var(mgr, var, dev, default_verb, &names_in_string);
	if (rc)
		return NULL;

	tokens = strdup(names_in_string);
	name = strtok(tokens, ",");
	while (name != NULL) {
		m_name = (struct mixer_name *)malloc(sizeof(struct mixer_name));
		m_name->name = strdup(name);
		DL_APPEND(names, m_name);
		name = strtok(NULL, ",");
	}
	free((void*)names_in_string);
	free(tokens);
	return names;
}

/* Exported Interface */

snd_use_case_mgr_t *ucm_create(const char *name)
{
	snd_use_case_mgr_t *mgr;
	int rc;

	if (!name)
		return NULL;

	rc = snd_use_case_mgr_open(&mgr, name);
	if (rc) {
		syslog(LOG_ERR, "Can not open ucm for card %s, rc = %d",
		       name, rc);
		return NULL;
	}

	rc = snd_use_case_set(mgr, "_verb", default_verb);
	if (rc) {
		syslog(LOG_ERR, "Can not set verb %s for card %s, rc = %d",
		       default_verb, name, rc);
		ucm_destroy(mgr);
		return NULL;
	}

	return mgr;
}

void ucm_destroy(snd_use_case_mgr_t *mgr)
{
	snd_use_case_mgr_close(mgr);
}

int ucm_swap_mode_exists(snd_use_case_mgr_t *mgr)
{
	return ucm_mod_exists_with_suffix(mgr, swap_mode_suffix);
}

int ucm_enable_swap_mode(snd_use_case_mgr_t *mgr, const char *node_name,
		       int enable)
{
	char *swap_mod = NULL;
	int rc;
	swap_mod = (char *)malloc(strlen(node_name) + 1 +
			strlen(swap_mode_suffix) + 1);
	if (!swap_mod)
		return -ENOMEM;
	sprintf(swap_mod, "%s %s", node_name, swap_mode_suffix);
	if (!ucm_mod_exists_with_name(mgr, swap_mod)) {
		syslog(LOG_ERR, "Can not find swap mode modifier %s.", swap_mod);
		free((void *)swap_mod);
		return -EPERM;
	}
	if (modifier_enabled(mgr, swap_mod) == !!enable) {
		free((void *)swap_mod);
		return 0;
	}
	rc = ucm_set_modifier_enabled(mgr, swap_mod, enable);
	free((void *)swap_mod);
	return rc;
}

int ucm_set_enabled(snd_use_case_mgr_t *mgr, const char *dev, int enable)
{
	if (device_enabled(mgr, dev) == !!enable)
		return 0;

	return snd_use_case_set(mgr, enable ? "_enadev" : "_disdev", dev);
}

char *ucm_get_flag(snd_use_case_mgr_t *mgr, const char *flag_name)
{
	char *flag_value = NULL;
	const char *value;
	int rc;

	/* Set device to empty string since flag is specified in verb section */
	rc = get_var(mgr, flag_name, "", default_verb, &value);
	if (!rc) {
		flag_value = strdup(value);
		free((void *)value);
	}

	return flag_value;
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

char *ucm_get_mic_positions(snd_use_case_mgr_t *mgr)
{
	char *control_name = NULL;
	const char *value;
	int rc;

	rc = get_var(mgr, mic_positions, "", default_verb, &value);
	if (!rc) {
		control_name = strdup(value);
		free((void *)value);
	}

	return control_name;
}

const char *ucm_get_override_type_name(snd_use_case_mgr_t *mgr,
					const char *dev)
{
	const char *override_type_name;
	int rc;

	rc = get_var(mgr, override_type_name_var, dev, default_verb,
		     &override_type_name);
	if (rc)
		return NULL;

	return override_type_name;
}

char *ucm_get_dev_for_jack(snd_use_case_mgr_t *mgr, const char *jack,
			   enum CRAS_STREAM_DIRECTION direction)
{
	return ucm_get_dev_for_var(mgr, jack_var, jack, direction);
}

char *ucm_get_dev_for_mixer(snd_use_case_mgr_t *mgr, const char *mixer,
			    enum CRAS_STREAM_DIRECTION dir)
{
	return ucm_get_dev_for_var(mgr, mixer_var, mixer, dir);
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

unsigned int ucm_get_min_buffer_level(snd_use_case_mgr_t *mgr)
{
	const char *val = NULL;
	int rc;

	rc = get_var(mgr, min_buffer_level_var, "", default_verb, &val);
	if (rc)
		return 0;

	return atoi(val);
}

unsigned int ucm_get_disable_software_volume(snd_use_case_mgr_t *mgr)
{
	const char *val = NULL;
	int rc;

	rc = get_var(mgr, disable_software_volume, "", default_verb, &val);
	if (rc)
		return 0;

	return atoi(val);
}

int ucm_get_max_software_gain(snd_use_case_mgr_t *mgr, const char *dev,
			      long *gain)
{
	const char *val = NULL;
	int rc;

	rc = get_var(mgr, max_software_gain, dev, default_verb, &val);
	if (rc)
		return rc;
	*gain = atoi(val);
	return 0;
}

const char *ucm_get_device_name_for_dev(
	snd_use_case_mgr_t *mgr, const char *dev,
	enum CRAS_STREAM_DIRECTION direction)
{
	if (direction == CRAS_STREAM_OUTPUT)
		return ucm_get_playback_device_name_for_dev(mgr, dev);
	else if (direction == CRAS_STREAM_INPUT)
		return ucm_get_capture_device_name_for_dev(mgr, dev);
	return NULL;
}

struct mixer_name *ucm_get_coupled_mixer_names(
		snd_use_case_mgr_t *mgr, const char *dev)
{
	return ucm_get_mixer_names(mgr, dev, coupled_mixers);
}

void ucm_free_mixer_names(struct mixer_name *names)
{
	struct mixer_name *m;
	DL_FOREACH(names, m) {
		DL_DELETE(names, m);
		free((void*)m->name);
		free(m);
	}
}

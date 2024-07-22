/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "cras/src/server/cras_alsa_ucm.h"

#include <alsa/asoundlib.h>
#include <alsa/use-case.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "cras/common/check.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/server/cras_alsa_mixer_name.h"
#include "cras/src/server/cras_alsa_ucm_section.h"
#include "cras_audio_format.h"
#include "cras_types.h"
#include "cras_util.h"
#include "third_party/strlcpy/strlcpy.h"
#include "third_party/utlist/utlist.h"

#define INVALID_JACK_SWITCH -1
// Length for ucm private prefix, len(_ucmXXXX.) == 9
#define UCM_PREFIX_LENGTH 9

static const char jack_control_var[] = "JackControl";
static const char jack_dev_var[] = "JackDev";
static const char jack_switch_var[] = "JackSwitch";
static const char edid_var[] = "EDIDFile";
static const char cap_var[] = "CaptureControl";
static const char override_type_name_var[] = "OverrideNodeType";
static const char dsp_name_var[] = "DspName";
static const char playback_mixer_elem_var[] = "PlaybackMixerElem";
static const char capture_mixer_elem_var[] = "CaptureMixerElem";
static const char min_buffer_level_var[] = "MinBufferLevel";
static const char dma_period_var[] = "DmaPeriodMicrosecs";
static const char use_software_volume[] = "UseSoftwareVolume";
static const char playback_device_name_var[] = "PlaybackPCM";
static const char playback_device_rate_var[] = "PlaybackRate";
static const char playback_channel_map_var[] = "PlaybackChannelMap";
static const char playback_channels_var[] = "PlaybackChannels";
static const char playback_number_of_volume_steps_var[] =
    "CRASPlaybackNumberOfVolumeSteps";
static const char capture_device_name_var[] = "CapturePCM";
static const char capture_device_rate_var[] = "CaptureRate";
static const char capture_channel_map_var[] = "CaptureChannelMap";
static const char capture_channels_var[] = "CaptureChannels";
static const char coupled_mixers[] = "CoupledMixers";
static const char dependent_device_name_var[] = "DependentPCM";
static const char preempt_hotword_var[] = "PreemptHotword";
static const char echo_reference_dev_name_var[] = "EchoReferenceDev";
static const char rtc_proc_echo_cancellation_modifier[] =
    "RTC Proc Echo Cancellation";
static const char rtc_proc_noise_suppression_modifier[] =
    "RTC Proc Noise Suppression";
static const char rtc_proc_gain_control_modifier[] = "RTC Proc Gain Control";

// SectionModifier prefixes and suffixes.
static const char hotword_model_prefix[] = "Hotword Model";
static const char swap_mode_suffix[] = "Swap Mode";
static const char noise_cancellation_suffix[] = "Noise Cancellation";

/*
 * Set this value in a SectionDevice to specify the intrinsic sensitivity in
 * 0.01 dBFS/Pa. It currently only supports input devices. You should get the
 * value by recording samples without either hardware or software gain. We are
 * still working on building a standard process for measuring it. The value you
 * see now in our UCM is just estimated value. If it is set, CRAS will enable
 * software gain and use the value as a reference for calculating the
 * appropriate software gain to apply to the device to meet our target volume.
 */
static const char intrinsic_sensitivity_var[] = "IntrinsicSensitivity";

/*
 * Set this value in a SectionDevice to specify the default node gain in
 * 0.01 dB.
 */
static const char default_node_gain[] = "DefaultNodeGain";
static const char fully_specified_ucm_var[] = "FullySpecifiedUCM";
static const char main_volume_names[] = "MainVolumeNames";

/*
 * Names of lists for snd_use_case_geti to use when looking up values
 */
static const char enabled_devices_list[] = "_devstatus";
static const char enabled_modifiers_list[] = "_modstatus";
static const char supported_devs_list[] = "_supporteddevs";

/*
 * Use case verbs corresponding to CRAS_USE_CASE. CRAS-specific use cases not
 * defined in alsa/use-case.h are prefixed with CRAS.
 */
static const char* use_case_verbs[] = {
    SND_USE_CASE_VERB_HIFI,
    "CRAS Low Latency",
    "CRAS Low Latency Raw",
};
static_assert(ARRAY_SIZE(use_case_verbs) == CRAS_NUM_USE_CASES,
              "Use case verb string/enum mismatch");

static const char ucm_path_prefix[] = "/usr/share/alsa/ucm";

static const size_t max_section_name_len = 100;

// Represents a list of section names found in UCM.
struct section_name {
  const char* name;
  struct section_name *prev, *next;
};

struct cras_use_case_mgr {
  snd_use_case_mgr_t* mgr;
  char* name;
  const char* private_prefix;
  cras_use_cases_t avail_use_cases;
  enum CRAS_USE_CASE use_case;
  char* hotword_modifier;
};

static inline const char* uc_verb(struct cras_use_case_mgr* mgr) {
  return use_case_verbs[mgr->use_case];
}

static int get_status(struct cras_use_case_mgr* mgr,
                      const char* list,
                      const char* target,
                      long* value) {
  char identifier[max_section_name_len];

  snprintf(identifier, sizeof(identifier), "%s/%s", list, target);
  return snd_use_case_geti(mgr->mgr, identifier, value);
}

static inline int modifier_enabled(struct cras_use_case_mgr* mgr,
                                   const char* modifier,
                                   long* value) {
  return get_status(mgr, enabled_modifiers_list, modifier, value);
}

static inline int device_enabled(struct cras_use_case_mgr* mgr,
                                 const char* dev,
                                 long* value) {
  return get_status(mgr, enabled_devices_list, dev, value);
}

static int get_var(struct cras_use_case_mgr* mgr,
                   const char* var,
                   const char* dev,
                   const char* verb,
                   const char** value) {
  char* id;
  int rc = asprintf(&id, "=%s/%s/%s", var, dev, verb);
  if (rc < 0) {
    return -ENOMEM;
  }

  rc = snd_use_case_get(mgr->mgr, id, value);
  free(id);

  return rc;
}

static int get_int(struct cras_use_case_mgr* mgr,
                   const char* var,
                   const char* dev,
                   const char* verb,
                   int* value) {
  const char* str_value;
  int rc;

  if (!value) {
    return -EINVAL;
  }
  rc = get_var(mgr, var, dev, verb, &str_value);
  if (rc != 0) {
    return rc;
  }
  rc = parse_int(str_value, value);
  if (rc < 0) {
    return rc;
  }
  free((void*)str_value);
  return 0;
}

static int ucm_set_modifier_enabled(struct cras_use_case_mgr* mgr,
                                    const char* mod,
                                    int enable) {
  int rc;

  rc = snd_use_case_set(mgr->mgr, enable ? "_enamod" : "_dismod", mod);
  if (rc) {
    syslog(LOG_WARNING, "Can not %s UCM modifier %s, rc = %d",
           enable ? "enable" : "disable", mod, rc);
  }
  return rc;
}

static int ucm_str_ends_with_suffix(const char* str, const char* suffix) {
  if (!str || !suffix) {
    return 0;
  }
  size_t len_str = strlen(str);
  size_t len_suffix = strlen(suffix);
  if (len_suffix > len_str) {
    return 0;
  }
  return strncmp(str + len_str - len_suffix, suffix, len_suffix) == 0;
}

static int ucm_section_exists_with_suffix(struct cras_use_case_mgr* mgr,
                                          const char* suffix,
                                          const char* identifier) {
  const char** list;
  unsigned int i;
  int num_entries;
  int exist = 0;

  num_entries = snd_use_case_get_list(mgr->mgr, identifier, &list);
  if (num_entries <= 0) {
    return num_entries;
  }

  for (i = 0; i < (unsigned int)num_entries; i += 2) {
    if (!list[i]) {
      continue;
    }

    if (ucm_str_ends_with_suffix(list[i], suffix)) {
      exist = 1;
      break;
    }
  }
  snd_use_case_free_list(list, num_entries);
  return exist;
}

static int ucm_mod_exists_with_suffix(struct cras_use_case_mgr* mgr,
                                      const char* suffix) {
  char* identifier;
  int rc;

  identifier = snd_use_case_identifier("_modifiers/%s", uc_verb(mgr));
  rc = ucm_section_exists_with_suffix(mgr, suffix, identifier);
  free(identifier);
  return rc;
}

static int ucm_mod_exists_with_name(struct cras_use_case_mgr* mgr,
                                    const char* name) {
  long value;
  int ret;
  ret = modifier_enabled(mgr, name, &value);
  if (ret == -ENOENT) {
    return 0;
  }
  return ret < 0 ? ret : 1;
}

// Get a list of section names whose variable is the matched value.
static struct section_name* ucm_get_sections_for_var(
    struct cras_use_case_mgr* mgr,
    const char* var,
    const char* value,
    const char* identifier,
    enum CRAS_STREAM_DIRECTION direction) {
  const char** list;
  struct section_name *section_names = NULL, *s_name;
  unsigned int i;
  int num_entries;
  int rc;

  num_entries = snd_use_case_get_list(mgr->mgr, identifier, &list);
  if (num_entries < 0) {
    syslog(LOG_WARNING, "Failed to get section UCM list for %s, error: %d",
           identifier, num_entries);
  }
  if (num_entries <= 0) {
    return NULL;
  }

  /* snd_use_case_get_list fills list with pairs of device name and
   * comment, so device names are in even-indexed elements. */
  for (i = 0; i < (unsigned int)num_entries; i += 2) {
    const char* this_value;

    if (!list[i]) {
      continue;
    }

    rc = get_var(mgr, var, list[i], uc_verb(mgr), &this_value);
    if (rc) {
      continue;
    }

    if (!strcmp(value, this_value)) {
      s_name = (struct section_name*)malloc(sizeof(*s_name));

      if (!s_name) {
        syslog(LOG_ERR, "Failed to allocate memory");
        free((void*)this_value);
        break;
      }

      s_name->name = strdup(list[i]);
      DL_APPEND(section_names, s_name);
    }
    free((void*)this_value);
  }

  snd_use_case_free_list(list, num_entries);
  return section_names;
}

static struct section_name* ucm_get_devices_for_var(
    struct cras_use_case_mgr* mgr,
    const char* var,
    const char* value,
    enum CRAS_STREAM_DIRECTION dir) {
  char* identifier;
  struct section_name* section_names;

  identifier = snd_use_case_identifier("_devices/%s", uc_verb(mgr));
  section_names = ucm_get_sections_for_var(mgr, var, value, identifier, dir);
  free(identifier);
  return section_names;
}

static const char* ucm_get_value_for_dev(struct cras_use_case_mgr* mgr,
                                         const char* value_var,
                                         const char* dev) {
  const char* name = NULL;
  int rc;

  rc = get_var(mgr, value_var, dev, uc_verb(mgr), &name);
  if (rc) {
    return NULL;
  }

  return name;
}

static inline const char* ucm_get_playback_device_name_for_dev(
    struct cras_use_case_mgr* mgr,
    const char* dev) {
  return ucm_get_value_for_dev(mgr, playback_device_name_var, dev);
}

static inline const char* ucm_get_capture_device_name_for_dev(
    struct cras_use_case_mgr* mgr,
    const char* dev) {
  return ucm_get_value_for_dev(mgr, capture_device_name_var, dev);
}

/* Gets the value of DependentPCM property. This is used to structure two
 * SectionDevices under one cras iodev to avoid two PCMs be open at the
 * same time because of restriction in lower layer driver or hardware.
 */
static inline const char* ucm_get_dependent_device_name_for_dev(
    struct cras_use_case_mgr* mgr,
    const char* dev) {
  return ucm_get_value_for_dev(mgr, dependent_device_name_var, dev);
}

/* Get a list of mixer names specified in a UCM variable separated by ",".
 * E.g. "Left Playback,Right Playback".
 */
static struct mixer_name* ucm_get_mixer_names(struct cras_use_case_mgr* mgr,
                                              const char* dev,
                                              const char* var,
                                              enum CRAS_STREAM_DIRECTION dir,
                                              mixer_name_type type) {
  const char* names_in_string = NULL;
  int rc;
  char *tokens, *name;
  char* laststr = NULL;
  struct mixer_name* names = NULL;

  rc = get_var(mgr, var, dev, uc_verb(mgr), &names_in_string);
  if (rc) {
    return NULL;
  }

  tokens = strdup(names_in_string);
  name = strtok_r(tokens, ",", &laststr);
  while (name != NULL) {
    names = mixer_name_add(names, name, dir, type);
    name = strtok_r(NULL, ",", &laststr);
  }
  free((void*)names_in_string);
  free(tokens);
  return names;
}

// Gets the modifier name of Noise Cancellation for the given node_name.
static void ucm_get_node_noise_cancellation_name(const char* node_name,
                                                 char* mod_name) {
  size_t len = strlen(node_name) + 1 + strlen(noise_cancellation_suffix) + 1;
  if (len > max_section_name_len) {
    syslog(LOG_ERR, "Length of the given section name is %zu > %zu(max)", len,
           max_section_name_len);
    len = max_section_name_len;
  }
  snprintf(mod_name, len, "%s %s", node_name, noise_cancellation_suffix);
}

// Exported Interface

int ucm_conf_exists(const char* name) {
  const size_t buf_size = 256;
  char file_path[buf_size];

  snprintf(file_path, buf_size, "%s/%s/%s.conf", ucm_path_prefix, name, name);
  if (access(file_path, F_OK) == 0) {
    return 1;
  }

  snprintf(file_path, buf_size, "%s2/%s/%s.conf", ucm_path_prefix, name, name);
  if (access(file_path, F_OK) == 0) {
    return 1;
  }

  return 0;
}

struct cras_use_case_mgr* ucm_create(const char* name) {
  struct cras_use_case_mgr* mgr;
  int rc;
  const char** list;
  int num_verbs, i, j;

  if (!name) {
    return NULL;
  }

  mgr = (struct cras_use_case_mgr*)malloc(sizeof(*mgr));
  if (!mgr) {
    return NULL;
  }

  mgr->name = strdup(name);
  if (!mgr->name) {
    goto cleanup;
  }

  rc = snd_use_case_mgr_open(&mgr->mgr, name);
  if (rc) {
    syslog(LOG_WARNING, "Can not open ucm for card %s, rc = %d", name, rc);
    goto cleanup;
  }

  mgr->avail_use_cases = 0;
  mgr->hotword_modifier = NULL;
  num_verbs = snd_use_case_get_list(mgr->mgr, "_verbs", &list);
  for (i = 0; i < num_verbs; i += 2) {
    for (j = 0; j < CRAS_NUM_USE_CASES; ++j) {
      if (strcmp(list[i], use_case_verbs[j]) == 0) {
        break;
      }
    }
    if (j < CRAS_NUM_USE_CASES) {
      mgr->avail_use_cases |= (1 << j);
      syslog(LOG_DEBUG, "UCM verb found: %s -> %s", list[i],
             cras_use_case_str((enum CRAS_USE_CASE)j));
    } else {
      syslog(LOG_WARNING, "Unknown UCM verb ignored: %s", list[i]);
    }
  }
  if (num_verbs > 0) {
    snd_use_case_free_list(list, num_verbs);
  }

  rc = ucm_set_use_case(mgr, CRAS_USE_CASE_HIFI);
  if (rc) {
    goto cleanup_mgr;
  }
  rc = ucm_enable_use_case(mgr);
  if (rc) {
    goto cleanup_mgr;
  }

  rc = snd_use_case_get(mgr->mgr, "_alibpref", &mgr->private_prefix);
  if (rc) {
    syslog(LOG_WARNING, "Error occurred when fetch ucm private prefix, rc = %d",
           rc);
    mgr->private_prefix = NULL;
  }

  return mgr;

cleanup_mgr:
  snd_use_case_mgr_close(mgr->mgr);
cleanup:
  free(mgr->name);
  free(mgr);
  return NULL;
}

void ucm_destroy(struct cras_use_case_mgr* mgr) {
  snd_use_case_mgr_close(mgr->mgr);
  free((void*)mgr->private_prefix);
  free(mgr->hotword_modifier);
  free(mgr->name);
  free(mgr);
}

cras_use_cases_t ucm_get_avail_use_cases(struct cras_use_case_mgr* mgr) {
  return mgr->avail_use_cases;
}

int ucm_set_use_case(struct cras_use_case_mgr* mgr,
                     enum CRAS_USE_CASE use_case) {
  if (mgr->avail_use_cases & (1 << use_case)) {
    mgr->use_case = use_case;
  } else {
    syslog(LOG_ERR, "Unavailable use case %s for card %s",
           cras_use_case_str(use_case), mgr->name);
    return -EINVAL;
  }

  return 0;
}

int ucm_enable_use_case(struct cras_use_case_mgr* mgr) {
  int rc;

  rc = snd_use_case_set(mgr->mgr, "_verb", uc_verb(mgr));
  if (rc) {
    syslog(LOG_ERR, "Can not set verb %s for card %s, rc = %d", uc_verb(mgr),
           mgr->name, rc);
    return rc;
  }

  return 0;
}

int ucm_swap_mode_exists(struct cras_use_case_mgr* mgr) {
  return ucm_mod_exists_with_suffix(mgr, swap_mode_suffix);
}

static int ucm_modifier_try_enable(struct cras_use_case_mgr* mgr,
                                   int enable,
                                   const char* name) {
  long value;
  int ret;

  if (!ucm_mod_exists_with_name(mgr, name)) {
    syslog(LOG_WARNING, "Can not find modifier %s.", name);
    return -ENOTSUP;
  }

  ret = modifier_enabled(mgr, name, &value);
  if (ret < 0) {
    syslog(LOG_WARNING, "Failed to check modifier: %d", ret);
    return ret;
  }

  if (value == !!enable) {
    syslog(LOG_DEBUG, "Modifier %s is already %s.", name,
           enable ? "enabled" : "disabled");
    return 0;
  }

  syslog(LOG_DEBUG, "UCM %s Modifier %s", enable ? "enable" : "disable", name);
  return ucm_set_modifier_enabled(mgr, name, enable);
}

int ucm_enable_swap_mode(struct cras_use_case_mgr* mgr,
                         const char* node_name,
                         int enable) {
  char swap_mod[max_section_name_len];

  snprintf(swap_mod, sizeof(swap_mod), "%s %s", node_name, swap_mode_suffix);
  return ucm_set_modifier_enabled(mgr, swap_mod, enable);
}

int inline ucm_node_echo_cancellation_exists(struct cras_use_case_mgr* mgr) {
  return ucm_mod_exists_with_name(mgr, rtc_proc_echo_cancellation_modifier);
}

int inline ucm_enable_node_echo_cancellation(struct cras_use_case_mgr* mgr,
                                             int enable) {
  return ucm_modifier_try_enable(mgr, enable,
                                 rtc_proc_echo_cancellation_modifier);
}

int inline ucm_node_noise_suppression_exists(struct cras_use_case_mgr* mgr) {
  return ucm_mod_exists_with_name(mgr, rtc_proc_noise_suppression_modifier);
}

int inline ucm_enable_node_noise_suppression(struct cras_use_case_mgr* mgr,
                                             int enable) {
  return ucm_modifier_try_enable(mgr, enable,
                                 rtc_proc_noise_suppression_modifier);
}

int inline ucm_node_gain_control_exists(struct cras_use_case_mgr* mgr) {
  return ucm_mod_exists_with_name(mgr, rtc_proc_gain_control_modifier);
}

int inline ucm_enable_node_gain_control(struct cras_use_case_mgr* mgr,
                                        int enable) {
  return ucm_modifier_try_enable(mgr, enable, rtc_proc_gain_control_modifier);
}

int ucm_node_noise_cancellation_exists(struct cras_use_case_mgr* mgr,
                                       const char* node_name) {
  char node_modifier_name[max_section_name_len];

  ucm_get_node_noise_cancellation_name(node_name, node_modifier_name);
  return ucm_mod_exists_with_name(mgr, node_modifier_name);
}

int ucm_enable_node_noise_cancellation(struct cras_use_case_mgr* mgr,
                                       const char* node_name,
                                       int enable) {
  char node_modifier_name[max_section_name_len];

  ucm_get_node_noise_cancellation_name(node_name, node_modifier_name);
  return ucm_modifier_try_enable(mgr, enable, node_modifier_name);
}

int ucm_set_enabled(struct cras_use_case_mgr* mgr,
                    const char* dev,
                    int enable) {
  int rc;
  long value;

  rc = device_enabled(mgr, dev, &value);
  if (rc < 0) {
    syslog(LOG_WARNING, "Failed to check device status in enable");
    return rc;
  }
  if (value == !!enable) {
    return 0;
  }
  syslog(LOG_DEBUG, "UCM %s %s", enable ? "enable" : "disable", dev);
  rc = snd_use_case_set(mgr->mgr, enable ? "_enadev" : "_disdev", dev);
  if (rc && (rc != -ENOENT || ucm_has_fully_specified_ucm_flag(mgr))) {
    syslog(LOG_WARNING, "Can not %s UCM for device %s, rc = %d",
           enable ? "enable" : "disable", dev, rc);
  }
  return rc;
}

char* ucm_get_flag(struct cras_use_case_mgr* mgr, const char* flag_name) {
  char* flag_value = NULL;
  const char* value;
  int rc;

  // Set device to empty string since flag is specified in verb section
  rc = get_var(mgr, flag_name, "", uc_verb(mgr), &value);
  if (!rc) {
    flag_value = strdup(value);
    free((void*)value);
  }

  return flag_value;
}

char* ucm_get_cap_control(struct cras_use_case_mgr* mgr, const char* ucm_dev) {
  char* control_name = NULL;
  const char* value;
  int rc;

  rc = get_var(mgr, cap_var, ucm_dev, uc_verb(mgr), &value);
  if (!rc) {
    control_name = strdup(value);
    free((void*)value);
  }

  return control_name;
}

inline const char* ucm_get_override_type_name(struct cras_use_case_mgr* mgr,
                                              const char* dev) {
  return ucm_get_value_for_dev(mgr, override_type_name_var, dev);
}

char* ucm_get_dev_for_jack(struct cras_use_case_mgr* mgr,
                           const char* jack,
                           enum CRAS_STREAM_DIRECTION direction) {
  struct section_name *section_names, *c;
  char* ret = NULL;

  section_names = ucm_get_devices_for_var(mgr, jack_dev_var, jack, direction);

  DL_FOREACH (section_names, c) {
    if (!strcmp(c->name, "Mic")) {
      // Skip mic section for output
      if (direction == CRAS_STREAM_OUTPUT) {
        continue;
      }
    } else {
      // Only check mic for input.
      if (direction == CRAS_STREAM_INPUT) {
        continue;
      }
    }
    ret = strdup(c->name);
    break;
  }

  DL_FOREACH (section_names, c) {
    DL_DELETE(section_names, c);
    free((void*)c->name);
    free(c);
  }

  return ret;
}

char* ucm_get_dev_for_mixer(struct cras_use_case_mgr* mgr,
                            const char* mixer,
                            enum CRAS_STREAM_DIRECTION dir) {
  struct section_name *section_names = NULL, *c;
  char* ret = NULL;

  if (dir == CRAS_STREAM_OUTPUT) {
    section_names =
        ucm_get_devices_for_var(mgr, playback_mixer_elem_var, mixer, dir);
  } else if (dir == CRAS_STREAM_INPUT) {
    section_names =
        ucm_get_devices_for_var(mgr, capture_mixer_elem_var, mixer, dir);
  }

  if (section_names) {
    ret = strdup(section_names->name);
  }

  DL_FOREACH (section_names, c) {
    DL_DELETE(section_names, c);
    free((void*)c->name);
    free(c);
  }

  return ret;
}

inline const char* ucm_get_edid_file_for_dev(struct cras_use_case_mgr* mgr,
                                             const char* dev) {
  return ucm_get_value_for_dev(mgr, edid_var, dev);
}

inline const char* ucm_get_dsp_name_for_dev(struct cras_use_case_mgr* mgr,
                                            const char* dev) {
  return ucm_get_value_for_dev(mgr, dsp_name_var, dev);
}

int ucm_get_min_buffer_level(struct cras_use_case_mgr* mgr,
                             unsigned int* level) {
  int value;
  int rc;

  rc = get_int(mgr, min_buffer_level_var, "", uc_verb(mgr), &value);
  if (rc) {
    return -ENOENT;
  }
  *level = value;

  return 0;
}

int ucm_get_use_software_volume(struct cras_use_case_mgr* mgr) {
  int value;
  int rc;

  rc = get_int(mgr, use_software_volume, "", uc_verb(mgr), &value);
  if (rc) {
    /* Default to use HW volume */
    return 0;
  }

  CRAS_CHECK(value == 0 || value == 1);
  return value;
}

int ucm_get_default_node_gain(struct cras_use_case_mgr* mgr,
                              const char* dev,
                              long* gain) {
  int value;
  int rc;

  rc = get_int(mgr, default_node_gain, dev, uc_verb(mgr), &value);
  if (rc) {
    return rc;
  }
  *gain = value;
  return 0;
}

int ucm_get_intrinsic_sensitivity(struct cras_use_case_mgr* mgr,
                                  const char* dev,
                                  long* sensitivity) {
  int value;
  int rc;

  rc = get_int(mgr, intrinsic_sensitivity_var, dev, uc_verb(mgr), &value);
  if (rc) {
    return rc;
  }
  *sensitivity = value;
  return 0;
}

int ucm_get_preempt_hotword(struct cras_use_case_mgr* mgr, const char* dev) {
  int value;
  int rc;

  rc = get_int(mgr, preempt_hotword_var, dev, uc_verb(mgr), &value);
  if (rc) {
    return 0;
  }
  return value;
}

static int get_device_index_from_target(struct cras_use_case_mgr* mgr,
                                        const char* target_device_name);

int ucm_get_alsa_dev_idx_for_dev(struct cras_use_case_mgr* mgr,
                                 const char* dev,
                                 enum CRAS_STREAM_DIRECTION direction) {
  const char* pcm_name = NULL;
  int dev_idx = -1;

  if (direction == CRAS_STREAM_OUTPUT) {
    pcm_name = ucm_get_playback_device_name_for_dev(mgr, dev);
  } else if (direction == CRAS_STREAM_INPUT) {
    pcm_name = ucm_get_capture_device_name_for_dev(mgr, dev);
  }

  if (pcm_name) {
    dev_idx = get_device_index_from_target(mgr, pcm_name);
    free((void*)pcm_name);
  }
  return dev_idx;
}

// Get FIRST prefix match
//
// caller must free result
char* get_list_item_by_prefix(struct cras_use_case_mgr* mgr,
                              const char* list,
                              const char* prefix) {
  const char** results;
  int n = snd_use_case_get_list(mgr->mgr, list, &results);

  if (n < 0) {
    return NULL;
  }

  char* result = NULL;
  for (int i = 0; i < n; i++) {
    if (!strncmp(results[i], prefix, strlen(prefix))) {
      result = strdup(results[i]);
      break;
    }
  }

  snd_use_case_free_list(results, n);

  return result;
}

// Can also return modifiers
static inline char* get_supported_device_by_prefix(
    struct cras_use_case_mgr* mgr,
    const char* dev,
    const char* prefix) {
  char list[max_section_name_len];
  snprintf(list, max_section_name_len, "%s/%s/%s", supported_devs_list, dev,
           uc_verb(mgr));

  return get_list_item_by_prefix(mgr, list, prefix);
}

const char* ucm_get_echo_reference_dev_name_for_dev(
    struct cras_use_case_mgr* mgr,
    const char* dev) {
  // just inline and return this once below todo is resolved
  char* name =
      get_supported_device_by_prefix(mgr, dev, SND_USE_CASE_MOD_ECHO_REF);

  if (name) {
    return name;
  }

  // TODO(b/298058909) remove me once all UCMs are updated
  return ucm_get_value_for_dev(mgr, echo_reference_dev_name_var, dev);
}

int ucm_get_sample_rate_for_dev(struct cras_use_case_mgr* mgr,
                                const char* dev,
                                enum CRAS_STREAM_DIRECTION direction) {
  int value;
  int rc;
  const char* var_name;

  if (direction == CRAS_STREAM_OUTPUT) {
    var_name = playback_device_rate_var;
  } else if (direction == CRAS_STREAM_INPUT) {
    var_name = capture_device_rate_var;
  } else {
    return -EINVAL;
  }

  rc = get_int(mgr, var_name, dev, uc_verb(mgr), &value);
  if (rc) {
    return rc;
  }

  return value;
}

int ucm_get_channels_for_dev(struct cras_use_case_mgr* mgr,
                             const char* dev,
                             enum CRAS_STREAM_DIRECTION direction,
                             size_t* channels) {
  int value;
  int rc;
  const char* var_name;

  if (direction == CRAS_STREAM_OUTPUT) {
    var_name = playback_channels_var;
  } else if (direction == CRAS_STREAM_INPUT) {
    var_name = capture_channels_var;
  } else {
    return -EINVAL;
  }

  rc = get_int(mgr, var_name, dev, uc_verb(mgr), &value);
  if (rc) {
    return rc;
  }
  if (value < 0) {
    return -EINVAL;
  }

  *channels = (size_t)value;
  return 0;
}

int ucm_get_playback_number_of_volume_steps_for_dev(
    struct cras_use_case_mgr* mgr,
    const char* dev,
    int32_t* playback_number_of_volume_steps) {
  int value;
  int rc;
  // TODO(b/317461596) abstract get_int usage when migrate to rust.
  rc = get_int(mgr, playback_number_of_volume_steps_var, dev, uc_verb(mgr),
               &value);
  if (rc) {
    return rc;
  }

  *playback_number_of_volume_steps = (int32_t)value;
  return 0;
}

int ucm_get_chmap_for_dev(struct cras_use_case_mgr* mgr,
                          const char* dev,
                          int8_t* channel_layout,
                          const char* var_name) {
  const char* var_str;
  char *tokens, *token;
  int i, rc;

  rc = get_var(mgr, var_name, dev, uc_verb(mgr), &var_str);
  if (rc) {
    return rc;
  }

  tokens = strdup(var_str);
  token = strtok(tokens, " ");
  for (i = 0; token && (i < CRAS_CH_MAX); i++) {
    int channel;
    rc = parse_int(token, &channel);
    if (rc < 0) {
      free((void*)tokens);
      free((void*)var_str);
      return rc;
    }
    channel_layout[i] = channel;
    token = strtok(NULL, " ");
  }

  free((void*)tokens);
  free((void*)var_str);
  return (i == CRAS_CH_MAX) ? 0 : -EINVAL;
}

int ucm_get_playback_chmap_for_dev(struct cras_use_case_mgr* mgr,
                                   const char* dev,
                                   int8_t* channel_layout) {
  return ucm_get_chmap_for_dev(mgr, dev, channel_layout,
                               playback_channel_map_var);
}

int ucm_get_capture_chmap_for_dev(struct cras_use_case_mgr* mgr,
                                  const char* dev,
                                  int8_t* channel_layout) {
  return ucm_get_chmap_for_dev(mgr, dev, channel_layout,
                               capture_channel_map_var);
}

struct mixer_name* ucm_get_coupled_mixer_names(struct cras_use_case_mgr* mgr,
                                               const char* dev) {
  return ucm_get_mixer_names(mgr, dev, coupled_mixers, CRAS_STREAM_OUTPUT,
                             MIXER_NAME_VOLUME);
}

// Check if target_device_name is a private ALSA device name in form
// _ucmXXXX.hw:card-name[,<num>], with XXXX matching the prefix for the current
// card.
static bool is_private_device_name(struct cras_use_case_mgr* mgr,
                                   const char* target_device_name) {
  return mgr->private_prefix != NULL &&
         !strncmp(target_device_name, mgr->private_prefix,
                  strnlen(mgr->private_prefix, UCM_PREFIX_LENGTH));
}

// Expects a string in the form: hw:card-name,<num> or
// _ucmXXXX.hw:card-name[,<num>]
static int get_device_index_from_target(struct cras_use_case_mgr* mgr,
                                        const char* target_device_name) {
  const char* pos = target_device_name;
  if (!pos) {
    return -EINVAL;
  }

  bool is_private = is_private_device_name(mgr, pos);
  if (is_private) {
    pos += strnlen(mgr->private_prefix, UCM_PREFIX_LENGTH);
  }

  while (*pos && *pos != ',') {
    ++pos;
  }
  if (*pos == ',') {
    ++pos;
    int index;
    int rc = parse_int(pos, &index);
    if (rc < 0) {
      return rc;
    }
    return index;
  }
  return (is_private ? 0 : -EINVAL);
}

static const char* ucm_get_dir_for_device(struct cras_use_case_mgr* mgr,
                                          const char* dev_name,
                                          enum CRAS_STREAM_DIRECTION* dir) {
  const char* pcm_name;

  pcm_name = ucm_get_playback_device_name_for_dev(mgr, dev_name);

  if (pcm_name) {
    *dir = CRAS_STREAM_OUTPUT;
    return pcm_name;
  }

  pcm_name = ucm_get_capture_device_name_for_dev(mgr, dev_name);
  if (pcm_name) {
    *dir = CRAS_STREAM_INPUT;
    return pcm_name;
  }

  *dir = CRAS_STREAM_UNDEFINED;
  return NULL;
}

static int ucm_parse_device_section(struct cras_use_case_mgr* mgr,
                                    const char* dev,
                                    struct ucm_section** sections) {
  enum CRAS_STREAM_DIRECTION dir;
  int dev_idx = -1;
  int dependent_dev_idx = -1;
  const char* jack_name = NULL;
  const char* jack_type = NULL;
  const char* jack_dev = NULL;
  const char* jack_control = NULL;
  const char* mixer_name = NULL;
  struct mixer_name* m_name;
  int rc = 0;
  const char* pcm_name;
  const char* dependent_dev_name = NULL;
  struct ucm_section* dev_sec;
  const char* dev_name;

  dev_name = strdup(dev);
  if (!dev_name) {
    return 0;
  }

  pcm_name = ucm_get_dir_for_device(mgr, dev_name, &dir);

  if (pcm_name) {
    dev_idx = get_device_index_from_target(mgr, pcm_name);
  }

  if (dir == CRAS_STREAM_UNDEFINED) {
    syslog(LOG_INFO,
           "UCM configuration for device '%s' does not have"
           " PlaybackPCM or CapturePCM definition.",
           dev_name);
    rc = 0;
    goto error_cleanup;
  }

  dependent_dev_name = ucm_get_dependent_device_name_for_dev(mgr, dev_name);
  if (dependent_dev_name) {
    dependent_dev_idx = get_device_index_from_target(mgr, dependent_dev_name);
  }

  jack_dev = ucm_get_jack_dev_for_dev(mgr, dev_name);
  jack_control = ucm_get_jack_control_for_dev(mgr, dev_name);
  if (dir == CRAS_STREAM_OUTPUT) {
    mixer_name = ucm_get_playback_mixer_elem_for_dev(mgr, dev_name);
  } else if (dir == CRAS_STREAM_INPUT) {
    mixer_name = ucm_get_capture_mixer_elem_for_dev(mgr, dev_name);
  }

  if (jack_dev) {
    jack_name = jack_dev;
    jack_type = "gpio";
  } else if (jack_control) {
    jack_name = jack_control;
    jack_type = "hctl";
  }

  dev_sec = ucm_section_create(dev_name, pcm_name, dev_idx, dependent_dev_idx,
                               dir, jack_name, jack_type);

  if (!dev_sec) {
    syslog(LOG_ERR, "Failed to allocate memory.");
    rc = -ENOMEM;
    goto error_cleanup;
  }

  dev_sec->jack_switch = ucm_get_jack_switch_for_dev(mgr, dev_name);

  if (mixer_name) {
    rc = ucm_section_set_mixer_name(dev_sec, mixer_name);
    if (rc) {
      goto error_cleanup;
    }
  }

  m_name = ucm_get_mixer_names(mgr, dev_name, coupled_mixers, dir,
                               MIXER_NAME_VOLUME);
  ucm_section_concat_coupled(dev_sec, m_name);

  DL_APPEND(*sections, dev_sec);
  ucm_section_dump(dev_sec);
error_cleanup:
  free((void*)dev_name);
  free((void*)dependent_dev_name);
  free((void*)jack_dev);
  free((void*)jack_control);
  free((void*)mixer_name);
  free((void*)pcm_name);
  return rc;
}

static int get_sections_by_type(struct cras_use_case_mgr* mgr,
                                struct ucm_section** sections,
                                const char* path_identifier) {
  const char** list;
  int num_devs;
  int i;
  char* identifier;
  int ret = 0;

  /* Find the list of all mixers using the control names defined in
   * the header definition for this function.  */
  identifier = snd_use_case_identifier("%s/%s", path_identifier, uc_verb(mgr));
  num_devs = snd_use_case_get_list(mgr->mgr, identifier, &list);
  free(identifier);

  if (num_devs < 0) {
    syslog(LOG_ERR, "Failed to get ucm sections for %s: %d", path_identifier,
           num_devs);
    return num_devs;
  }

  /* snd_use_case_get_list fills list with pairs of device name and
   * comment, so device names are in even-indexed elements. */
  for (i = 0; i < num_devs; i += 2) {
    ret = ucm_parse_device_section(mgr, list[i], sections);
    if (ret < 0) {
      break;
    }
  }

  if (num_devs > 0) {
    snd_use_case_free_list(list, num_devs);
  }
  return ret;
}

struct ucm_section* ucm_get_sections(struct cras_use_case_mgr* mgr) {
  struct ucm_section* sections = NULL;

  if (get_sections_by_type(mgr, &sections, "_devices") < 0) {
    goto ucm_err;
  }
  if (get_sections_by_type(mgr, &sections, "_modifiers") < 0) {
    goto ucm_err;
  }

  return sections;
ucm_err:
  ucm_section_free_list(sections);
  return NULL;
}

char* ucm_get_hotword_models(struct cras_use_case_mgr* mgr) {
  const char** list;
  int i, num_entries;
  int models_len = 0;
  int ret = -ENOMEM;
  char* models = NULL;
  const char* model_name;
  char* identifier;
  size_t buf_size;

  identifier = snd_use_case_identifier("_modifiers/%s", uc_verb(mgr));
  if (!identifier) {
    goto err;
  }

  num_entries = snd_use_case_get_list(mgr->mgr, identifier, &list);
  free(identifier);
  if (num_entries < 0) {
    ret = num_entries;
    goto err;
  }

  if (num_entries == 0) {
    return NULL;
  }

  buf_size = num_entries * (CRAS_MAX_HOTWORD_MODEL_NAME_SIZE + 1);
  models = (char*)malloc(buf_size);
  if (!models) {
    snd_use_case_free_list(list, num_entries);
    goto err;
  }

  for (i = 0; i < num_entries; i += 2) {
    if (!list[i]) {
      continue;
    }

    if (strncmp(list[i], hotword_model_prefix, strlen(hotword_model_prefix))) {
      continue;
    }

    model_name = list[i] + strlen(hotword_model_prefix);
    while (isspace(*model_name)) {
      model_name++;
    }

    if (strlen(model_name) > CRAS_MAX_HOTWORD_MODEL_NAME_SIZE) {
      syslog(LOG_ERR,
             "Ignore hotword model %s because the it is"
             "too long.",
             list[i]);
      continue;
    }

    if (models_len != 0) {
      models[models_len++] = ',';
    }

    strlcpy(models + models_len, model_name, buf_size - models_len);
    models_len += strlen(model_name);
  }
  models[models_len++] = 0;
  snd_use_case_free_list(list, num_entries);

  return models;
err:
  syslog(LOG_WARNING, "Failed to get hotword due to error: %d", ret);
  return NULL;
}

void ucm_disable_all_hotword_models(struct cras_use_case_mgr* mgr) {
  const char** list;
  int num_enmods, mod_idx;

  if (!mgr) {
    return;
  }

  // Disable all currently enabled hotword model modifiers.
  num_enmods = snd_use_case_get_list(mgr->mgr, "_enamods", &list);
  if (num_enmods < 0) {
    syslog(LOG_WARNING, "Failed to get hotword model list: %d", num_enmods);
  }
  if (num_enmods <= 0) {
    return;
  }

  for (mod_idx = 0; mod_idx < num_enmods; mod_idx++) {
    if (!strncmp(list[mod_idx], hotword_model_prefix,
                 strlen(hotword_model_prefix))) {
      ucm_set_modifier_enabled(mgr, list[mod_idx], 0);
    }
  }
  snd_use_case_free_list(list, num_enmods);
}

int ucm_enable_hotword_model(struct cras_use_case_mgr* mgr) {
  long mod_status;
  int ret;

  if (!mgr->hotword_modifier) {
    return -EINVAL;
  }

  ret = modifier_enabled(mgr, mgr->hotword_modifier, &mod_status);
  if (ret < 0) {
    return ret;
  }

  if (!mod_status) {
    return ucm_set_modifier_enabled(mgr, mgr->hotword_modifier, 1);
  }

  return -EINVAL;
}

int ucm_set_hotword_model(struct cras_use_case_mgr* mgr, const char* model) {
  char* model_mod;
  long mod_status = 0;
  size_t model_mod_size = strlen(model) + 1 + strlen(hotword_model_prefix) + 1;

  model_mod = (char*)malloc(model_mod_size);

  if (!model_mod) {
    return -ENOMEM;
  }
  snprintf(model_mod, model_mod_size, "%s %s", hotword_model_prefix, model);
  if (!ucm_mod_exists_with_name(mgr, model_mod)) {
    free((void*)model_mod);
    return -EINVAL;
  }

  if (mgr->hotword_modifier &&
      !strncmp(mgr->hotword_modifier, model_mod, model_mod_size)) {
    free((void*)model_mod);
    return 0;
  }

  // If check failed, just move on, dont fail incoming model
  if (mgr->hotword_modifier) {
    modifier_enabled(mgr, mgr->hotword_modifier, &mod_status);
  }

  ucm_disable_all_hotword_models(mgr);
  free(mgr->hotword_modifier);
  mgr->hotword_modifier = model_mod;
  if (mod_status) {
    return ucm_set_modifier_enabled(mgr, mgr->hotword_modifier, 1);
  }
  return 0;
}

int ucm_has_fully_specified_ucm_flag(struct cras_use_case_mgr* mgr) {
  char* flag;
  int ret = 0;
  flag = ucm_get_flag(mgr, fully_specified_ucm_var);
  if (!flag) {
    return 0;
  }
  ret = !strcmp(flag, "1");
  free(flag);
  return ret;
}

inline const char* ucm_get_playback_mixer_elem_for_dev(
    struct cras_use_case_mgr* mgr,
    const char* dev) {
  return ucm_get_value_for_dev(mgr, playback_mixer_elem_var, dev);
}

inline const char* ucm_get_capture_mixer_elem_for_dev(
    struct cras_use_case_mgr* mgr,
    const char* dev) {
  return ucm_get_value_for_dev(mgr, capture_mixer_elem_var, dev);
}

struct mixer_name* ucm_get_main_volume_names(struct cras_use_case_mgr* mgr) {
  return ucm_get_mixer_names(mgr, "", main_volume_names, CRAS_STREAM_OUTPUT,
                             MIXER_NAME_MAIN_VOLUME);
}

int ucm_list_section_devices_by_device_name(
    struct cras_use_case_mgr* mgr,
    enum CRAS_STREAM_DIRECTION direction,
    const char* device_name,
    ucm_list_section_devices_callback cb,
    void* cb_arg) {
  int listed = 0;
  struct section_name *section_names, *c;
  const char* var;
  char* identifier;

  if (direction == CRAS_STREAM_OUTPUT) {
    var = playback_device_name_var;
  } else if (direction == CRAS_STREAM_INPUT) {
    var = capture_device_name_var;
  } else {
    return 0;
  }

  identifier = snd_use_case_identifier("_devices/%s", uc_verb(mgr));
  section_names =
      ucm_get_sections_for_var(mgr, var, device_name, identifier, direction);
  free(identifier);
  if (!section_names) {
    return 0;
  }

  DL_FOREACH (section_names, c) {
    cb(c->name, cb_arg);
    listed++;
  }

  DL_FOREACH (section_names, c) {
    DL_DELETE(section_names, c);
    free((void*)c->name);
    free(c);
  }
  return listed;
}

inline const char* ucm_get_jack_control_for_dev(struct cras_use_case_mgr* mgr,
                                                const char* dev) {
  return ucm_get_value_for_dev(mgr, jack_control_var, dev);
}

inline const char* ucm_get_jack_dev_for_dev(struct cras_use_case_mgr* mgr,
                                            const char* dev) {
  return ucm_get_value_for_dev(mgr, jack_dev_var, dev);
}

int ucm_get_jack_switch_for_dev(struct cras_use_case_mgr* mgr,
                                const char* dev) {
  int value;

  int rc = get_int(mgr, jack_switch_var, dev, uc_verb(mgr), &value);
  if (rc || value < 0) {
    return JACK_SWITCH_AUTO_DETECT;
  }
  return value;
}

unsigned int ucm_get_dma_period_for_dev(struct cras_use_case_mgr* mgr,
                                        const char* dev) {
  int value;

  int rc = get_int(mgr, dma_period_var, dev, uc_verb(mgr), &value);
  if (rc || value < 0) {
    return 0;
  }
  return value;
}

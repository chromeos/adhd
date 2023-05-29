/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/config/cras_board_config.h"

#include <dictionary.h>
#include <errno.h>
#include <iniparser.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "cras/common/check.h"
#include "cras/src/server/iniparser_wrapper.h"
#include "cras_util.h"

#define board_offset(x) (offsetof(struct cras_board_config, x))

#define CONFIG_NAME "board.ini"

#define UCM_IGNORE_SUFFIX_KEY "ucm:ignore_suffix"

#define DSP_OFFLOAD_MAP_KEY "processing:dsp_offload_map"

#define DSP_OFFLOAD_MAP_DEFAULT "Speaker:(1,)"

struct ini_int_field {
  int32_t default_value;
  int32_t offset;
  const char* key;
};

// clang-format off
static const struct ini_int_field INI_INT_KEYS[] = {
    {512,  board_offset(default_output_buffer_size),   "output:default_output_buffer_size"},
    {0,    board_offset(aec_supported),                "processing:aec_supported"},
    {-1,   board_offset(aec_group_id),                 "processing:group_id"},
    {0,    board_offset(ns_supported),                 "processing:ns_supported"},
    {0,    board_offset(agc_supported),                "processing:agc_supported"},
    {0,    board_offset(nc_supported),                 "processing:nc_supported"},
    {1,    board_offset(hw_echo_ref_disabled),         "processing:hw_echo_ref_disabled"},
    {0,    board_offset(aec_on_dsp_supported),         "processing:aec_on_dsp_supported"},
    {0,    board_offset(ns_on_dsp_supported),          "processing:ns_on_dsp_supported"},
    {0,    board_offset(agc_on_dsp_supported),         "processing:agc_on_dsp_supported"},
    {1,    board_offset(bt_wbs_enabled),               "bluetooth:wbs_enabled"},
    {1,    board_offset(bt_hfp_offload_finch_applied), "bluetooth:hfp_offload_finch_applied"},
    {0,    board_offset(deprioritize_bt_wbs_mic),       "bluetooth:deprioritize_wbs_mic"},
    {0,    board_offset(hotword_pause_at_suspend),      "hotword:pause_at_suspend"},
    {2000, board_offset(max_internal_mic_gain),         "input:max_internal_mic_gain"},
    {2,    board_offset(max_internal_speaker_channels), "output:max_internal_speaker_channels"},
    // max_headphone_channels applied to both headphone and lineout.
    {2,    board_offset(max_headphone_channels),        "output:max_headphone_channels"},
    {0,    board_offset(nc_standalone_mode),            "processing:nc_standalone_mode"},
    {0,    board_offset(speaker_output_latency_offset_ms),"output:speaker_output_latency_offset_ms"},
    {0,    board_offset(output_proc_hats),              "output:output_proc_hats"},
    {0,    board_offset(using_default_volume_curve_for_usb_audio_device),"usb:using_default_volume_curve_for_usb_audio_device"},
};
// clang-format on

struct cras_board_config* cras_board_config_create(const char* config_path) {
  struct cras_board_config* board_config =
      (struct cras_board_config*)calloc(1, sizeof(*board_config));
  if (!board_config) {
    syslog(LOG_ERR, "Failed to allocate memory for cras_board_config");
    return NULL;
  }

  char ini_name[MAX_INI_NAME_LENGTH + 1] = {};
  dictionary* ini = NULL;
  uint8_t* cfg_ptr = (uint8_t*)board_config;

  if (config_path != NULL) {
    // Load the config only if config_path is set.
    snprintf(ini_name, MAX_INI_NAME_LENGTH, "%s/%s", config_path, CONFIG_NAME);
    ini = iniparser_load_wrapper(ini_name);
    if (ini == NULL) {
      syslog(LOG_DEBUG, "No ini file %s", ini_name);
    }
  }

  if (ini == NULL) {
    // No valid ini. Create empty dictionary to set defaults.
    ini = dictionary_new(0);
    CRAS_CHECK(ini != NULL);
    snprintf(ini_name, sizeof(ini_name), "<none>");
  }

  for (int i = 0; i < ARRAY_SIZE(INI_INT_KEYS); i++) {
    *((int32_t*)(cfg_ptr + INI_INT_KEYS[i].offset)) = iniparser_getint(
        ini, INI_INT_KEYS[i].key, INI_INT_KEYS[i].default_value);
  }

  const char* ptr = iniparser_getstring(ini, UCM_IGNORE_SUFFIX_KEY, "");
  if (ptr) {
    board_config->ucm_ignore_suffix = strdup(ptr);
    if (!board_config->ucm_ignore_suffix) {
      syslog(LOG_ERR, "Failed to call strdup: %d", errno);
    }
  }

  ptr = iniparser_getstring(ini, DSP_OFFLOAD_MAP_KEY, DSP_OFFLOAD_MAP_DEFAULT);
  if (ptr) {
    board_config->dsp_offload_map = strdup(ptr);
    if (!board_config->dsp_offload_map) {
      syslog(LOG_ERR, "Failed to call strdup: %d", errno);
    }
  }

  iniparser_freedict(ini);
  syslog(LOG_DEBUG, "Loaded ini file %s", ini_name);
  return board_config;
}

void cras_board_config_destroy(struct cras_board_config* board_config) {
  if (board_config) {
    free(board_config->ucm_ignore_suffix);
    free(board_config->dsp_offload_map);
  }
  free(board_config);
}

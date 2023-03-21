/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/config/cras_board_config.h"

#include <errno.h>
#include <syslog.h>

#include "cras/src/server/iniparser_wrapper.h"

static const int32_t DEFAULT_OUTPUT_BUFFER_SIZE = 512;
static const int32_t AEC_SUPPORTED_DEFAULT = 0;
static const int32_t AEC_GROUP_ID_DEFAULT = -1;
static const int32_t NS_SUPPORTED_DEFAULT = 0;
static const int32_t AGC_SUPPORTED_DEFAULT = 0;
static const int32_t NC_SUPPORTED_DEFAULT = 0;
static const int32_t HW_ECHO_REF_DISABLED_DEFAULT = 0;
static const int32_t AEC_ON_DSP_SUPPORTED_DEFAULT = 0;
static const int32_t NS_ON_DSP_SUPPORTED_DEFAULT = 0;
static const int32_t AGC_ON_DSP_SUPPORTED_DEFAULT = 0;
static const int32_t BLUETOOTH_WBS_ENABLED_INI_DEFAULT = 1;
static const int32_t BLUETOOTH_HFP_OFFLOAD_FINCH_APPLIED_INI_DEFAULT = 1;
static const int32_t BLUETOOTH_DEPRIORITIZE_WBS_MIC_INI_DEFAULT = 0;
static const int32_t HOTWORD_PAUSE_AT_SUSPEND_DEFAULT = 0;
static const int32_t MAX_INTERNAL_MIC_GAIN_DEFAULT = 2000;
static const int32_t MAX_INTERNAL_SPK_CHANNELS_DEFAULT = 2;
// MAX_HEADPHONE_CHANNELS_DEFAULT applied to both headphone and lineout.
static const int32_t MAX_HEADPHONE_CHANNELS_DEFAULT = 2;
static const int32_t NC_STANDALONE_MODE_DEFAULT = 0;

#define CONFIG_NAME "board.ini"
#define DEFAULT_OUTPUT_BUF_SIZE_INI_KEY "output:default_output_buffer_size"
#define AEC_SUPPORTED_INI_KEY "processing:aec_supported"
#define AEC_GROUP_ID_INI_KEY "processing:group_id"
#define NS_SUPPORTED_INI_KEY "processing:ns_supported"
#define AGC_SUPPORTED_INI_KEY "processing:agc_supported"
#define NC_SUPPORTED_INI_KEY "processing:nc_supported"
#define HW_ECHO_REF_DISABLED_KEY "processing:hw_echo_ref_disabled"
#define AEC_ON_DSP_SUPPORTED_INI_KEY "processing:aec_on_dsp_supported"
#define NS_ON_DSP_SUPPORTED_INI_KEY "processing:ns_on_dsp_supported"
#define AGC_ON_DSP_SUPPORTED_INI_KEY "processing:agc_on_dsp_supported"
#define BLUETOOTH_WBS_ENABLED_INI_KEY "bluetooth:wbs_enabled"
#define BLUETOOTH_HFP_OFFLOAD_FINCH_APPLIED_INI_KEY \
  "bluetooth:hfp_offload_finch_applied"
#define BLUETOOTH_DEPRIORITIZE_WBS_MIC_INI_KEY "bluetooth:deprioritize_wbs_mic"
#define UCM_IGNORE_SUFFIX_KEY "ucm:ignore_suffix"
#define HOTWORD_PAUSE_AT_SUSPEND "hotword:pause_at_suspend"
#define MAX_INTERNAL_MIC_GAIN "input:max_internal_mic_gain"
#define MAX_INTERNAL_SPK_CHANNELS_INI_KEY "output:max_internal_speaker_channels"
#define MAX_HEADPHONE_CHANNELS_INI_KEY "output:max_headphone_channels"
#define NC_STANDALONE_MODE_INI_KEY "processing:nc_standalone_mode"

void cras_board_config_get(const char* config_path,
                           struct cras_board_config* board_config) {
  char ini_name[MAX_INI_NAME_LENGTH + 1];
  char ini_key[MAX_INI_KEY_LENGTH + 1];
  const char* ptr;
  dictionary* ini;

  board_config->default_output_buffer_size = DEFAULT_OUTPUT_BUFFER_SIZE;
  board_config->aec_supported = AEC_SUPPORTED_DEFAULT;
  board_config->aec_group_id = AEC_GROUP_ID_DEFAULT;
  board_config->ns_supported = NS_SUPPORTED_DEFAULT;
  board_config->agc_supported = AGC_SUPPORTED_DEFAULT;
  board_config->nc_supported = NC_SUPPORTED_DEFAULT;
  board_config->hw_echo_ref_disabled = HW_ECHO_REF_DISABLED_DEFAULT;
  board_config->aec_on_dsp_supported = AEC_ON_DSP_SUPPORTED_DEFAULT;
  board_config->ns_on_dsp_supported = NS_ON_DSP_SUPPORTED_DEFAULT;
  board_config->agc_on_dsp_supported = AGC_ON_DSP_SUPPORTED_DEFAULT;
  board_config->ucm_ignore_suffix = NULL;
  board_config->bt_wbs_enabled = BLUETOOTH_WBS_ENABLED_INI_DEFAULT;
  board_config->bt_hfp_offload_finch_applied =
      BLUETOOTH_HFP_OFFLOAD_FINCH_APPLIED_INI_DEFAULT;
  board_config->deprioritize_bt_wbs_mic =
      BLUETOOTH_DEPRIORITIZE_WBS_MIC_INI_DEFAULT;
  board_config->max_internal_mic_gain = MAX_INTERNAL_MIC_GAIN_DEFAULT;
  board_config->max_internal_speaker_channels =
      MAX_INTERNAL_SPK_CHANNELS_DEFAULT;
  board_config->max_headphone_channels = MAX_HEADPHONE_CHANNELS_DEFAULT;
  board_config->nc_standalone_mode = NC_STANDALONE_MODE_DEFAULT;
  if (config_path == NULL) {
    return;
  }

  snprintf(ini_name, MAX_INI_NAME_LENGTH, "%s/%s", config_path, CONFIG_NAME);
  ini_name[MAX_INI_NAME_LENGTH] = '\0';
  ini = iniparser_load_wrapper(ini_name);
  if (ini == NULL) {
    syslog(LOG_DEBUG, "No ini file %s", ini_name);
    return;
  }

  snprintf(ini_key, MAX_INI_KEY_LENGTH, DEFAULT_OUTPUT_BUF_SIZE_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->default_output_buffer_size =
      iniparser_getint(ini, ini_key, DEFAULT_OUTPUT_BUFFER_SIZE);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, AEC_SUPPORTED_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->aec_supported =
      iniparser_getint(ini, ini_key, AEC_SUPPORTED_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, AEC_GROUP_ID_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->aec_group_id =
      iniparser_getint(ini, ini_key, AEC_GROUP_ID_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, NS_SUPPORTED_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->ns_supported =
      iniparser_getint(ini, ini_key, NS_SUPPORTED_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, AGC_SUPPORTED_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->agc_supported =
      iniparser_getint(ini, ini_key, AGC_SUPPORTED_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, NC_SUPPORTED_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->nc_supported =
      iniparser_getint(ini, ini_key, NC_SUPPORTED_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, HW_ECHO_REF_DISABLED_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->hw_echo_ref_disabled =
      iniparser_getint(ini, ini_key, HW_ECHO_REF_DISABLED_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, AEC_ON_DSP_SUPPORTED_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->aec_on_dsp_supported =
      iniparser_getint(ini, ini_key, AEC_ON_DSP_SUPPORTED_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, NS_ON_DSP_SUPPORTED_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->ns_on_dsp_supported =
      iniparser_getint(ini, ini_key, NS_ON_DSP_SUPPORTED_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, AGC_ON_DSP_SUPPORTED_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->agc_on_dsp_supported =
      iniparser_getint(ini, ini_key, AGC_ON_DSP_SUPPORTED_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, BLUETOOTH_WBS_ENABLED_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->bt_wbs_enabled =
      iniparser_getint(ini, ini_key, BLUETOOTH_WBS_ENABLED_INI_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH,
           BLUETOOTH_HFP_OFFLOAD_FINCH_APPLIED_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->bt_hfp_offload_finch_applied = iniparser_getint(
      ini, ini_key, BLUETOOTH_HFP_OFFLOAD_FINCH_APPLIED_INI_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, BLUETOOTH_DEPRIORITIZE_WBS_MIC_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->deprioritize_bt_wbs_mic = iniparser_getint(
      ini, ini_key, BLUETOOTH_DEPRIORITIZE_WBS_MIC_INI_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, UCM_IGNORE_SUFFIX_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  ptr = iniparser_getstring(ini, ini_key, "");
  if (ptr) {
    board_config->ucm_ignore_suffix = strdup(ptr);
    if (!board_config->ucm_ignore_suffix) {
      syslog(LOG_ERR, "Failed to call strdup: %d", errno);
    }
  }

  snprintf(ini_key, MAX_INI_KEY_LENGTH, HOTWORD_PAUSE_AT_SUSPEND);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->hotword_pause_at_suspend =
      iniparser_getint(ini, ini_key, HOTWORD_PAUSE_AT_SUSPEND_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, MAX_INTERNAL_MIC_GAIN);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->max_internal_mic_gain =
      iniparser_getint(ini, ini_key, MAX_INTERNAL_MIC_GAIN_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, MAX_INTERNAL_SPK_CHANNELS_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->max_internal_speaker_channels =
      iniparser_getint(ini, ini_key, MAX_INTERNAL_SPK_CHANNELS_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, MAX_HEADPHONE_CHANNELS_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->max_headphone_channels =
      iniparser_getint(ini, ini_key, MAX_HEADPHONE_CHANNELS_DEFAULT);

  snprintf(ini_key, MAX_INI_KEY_LENGTH, NC_STANDALONE_MODE_INI_KEY);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  board_config->nc_standalone_mode =
      iniparser_getint(ini, ini_key, NC_STANDALONE_MODE_DEFAULT);

  iniparser_freedict(ini);
  syslog(LOG_DEBUG, "Loaded ini file %s", ini_name);
}

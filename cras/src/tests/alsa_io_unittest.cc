// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <map>
#include <stdio.h>
#include <syslog.h>
#include <vector>

#include "cras/src/common/cras_alsa_card_info.h"
#include "cras/src/server/config/cras_board_config.h"
#include "cras/src/server/cras_alsa_mixer.h"
#include "cras/src/server/cras_dsp.h"
#include "cras/src/server/cras_dsp_offload.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_system_state.h"
#include "cras_shm.h"
#include "cras_types.h"

extern "C" {
//  Include C file to test static functions.
#include "cras/src/server/cras_alsa_io.c"
}

#define BUFFER_SIZE 8192

//  Data for simulating functions stubbed below.
static int cras_alsa_open_called;
static int cras_iodev_append_stream_ret;
static int cras_alsa_get_avail_frames_ret;
static int cras_alsa_get_avail_frames_avail;
static int cras_alsa_start_called;
static uint8_t* cras_alsa_mmap_begin_buffer;
static size_t cras_alsa_mmap_begin_frames;
static size_t cras_alsa_fill_properties_called;
static bool cras_alsa_support_8_channels;
static size_t alsa_mixer_set_dBFS_called;
static int alsa_mixer_set_dBFS_value;
static const struct mixer_control* alsa_mixer_set_dBFS_output;
static size_t alsa_mixer_set_capture_dBFS_called;
static int alsa_mixer_set_capture_dBFS_value;
static const struct mixer_control* alsa_mixer_set_capture_dBFS_input;
static const struct mixer_control*
    cras_alsa_mixer_get_minimum_capture_gain_mixer_input;
static const struct mixer_control*
    cras_alsa_mixer_get_maximum_capture_gain_mixer_input;
static size_t cras_alsa_mixer_list_outputs_called;
static size_t cras_alsa_mixer_list_inputs_called;
static size_t cras_alsa_mixer_get_control_for_section_called;
static struct mixer_control*
    cras_alsa_mixer_get_control_for_section_return_value;
static size_t sys_get_volume_called;
static size_t sys_get_volume_return_value;
static size_t alsa_mixer_set_mute_called;
static int alsa_mixer_set_mute_value;
static size_t cras_alsa_mixer_get_playback_dBFS_range_called;
static long cras_alsa_mixer_get_playback_dBFS_range_max;
static long cras_alsa_mixer_get_playback_dBFS_range_min;
static size_t cras_alsa_mixer_get_playback_step_called;
typedef std::map<const struct mixer_control*, int> PlaybackStepMap;
static PlaybackStepMap cras_alsa_mixer_get_playback_step_values;
static const struct mixer_control* alsa_mixer_set_mute_output;
static size_t sys_get_mute_called;
static int sys_get_mute_return_value;
static struct cras_alsa_mixer* fake_mixer = (struct cras_alsa_mixer*)1;
static struct cras_card_config* fake_config = (struct cras_card_config*)2;
static struct mixer_control** cras_alsa_mixer_list_outputs_outputs;
static size_t cras_alsa_mixer_list_outputs_outputs_length;
static struct mixer_control** cras_alsa_mixer_list_inputs_outputs;
static size_t cras_alsa_mixer_list_inputs_outputs_length;
static size_t cras_alsa_mixer_set_output_active_state_called;
static std::vector<struct mixer_control*>
    cras_alsa_mixer_set_output_active_state_outputs;
static std::vector<int> cras_alsa_mixer_set_output_active_state_values;
static cras_audio_format* fake_format;
static size_t sys_set_volume_limits_called;
static size_t cras_alsa_mixer_get_minimum_capture_gain_called;
static size_t cras_alsa_mixer_get_maximum_capture_gain_called;
static struct mixer_control* cras_alsa_jack_get_mixer_ret;
static size_t cras_alsa_mixer_get_output_volume_curve_called;
typedef std::map<const struct mixer_control*, std::string> ControlNameMap;
static ControlNameMap cras_alsa_mixer_get_control_name_values;
static size_t cras_alsa_mixer_get_control_name_called;
static size_t cras_alsa_jack_list_create_called;
static size_t cras_alsa_jack_list_find_jacks_by_name_matching_called;
static size_t cras_alsa_jack_found_num;
static const struct cras_alsa_jack* cras_alsa_jack_found_val[4];
static size_t cras_alsa_jack_list_add_jack_for_section_called;
static struct cras_alsa_jack*
    cras_alsa_jack_list_add_jack_for_section_result_jack;
static size_t cras_alsa_jack_list_destroy_called;
static int cras_alsa_jack_list_has_hctl_jacks_return_val;
static jack_state_change_callback* jack_plug_cb;
static void* jack_plug_cb_data;
static char test_card_name[] = "TestCard";
static char test_pcm_name[] = "TestPCM";
static char test_dev_name[] = "TestDev";
static char test_dev_id[] = "TestDevId";
static size_t cras_iodev_add_node_called;
static struct cras_ionode* cras_iodev_set_node_plugged_ionode;
static size_t cras_iodev_set_node_plugged_called;
static int cras_iodev_set_node_plugged_value;
static unsigned cras_alsa_jack_enable_ucm_called;
static unsigned ucm_set_enabled_called;
static size_t cras_iodev_update_dsp_called;
static const char* cras_iodev_update_dsp_name;
typedef std::map<const char*, std::string> DspNameMap;
static size_t ucm_get_dsp_name_for_dev_called;
static DspNameMap ucm_get_dsp_name_for_dev_values;
static size_t cras_iodev_free_resources_called;
static size_t cras_alsa_jack_update_node_type_called;
static int ucm_swap_mode_exists_ret_value;
static int ucm_enable_swap_mode_ret_value;
static size_t ucm_enable_swap_mode_called;
static int is_utf8_string_ret_value;
static const char* cras_alsa_jack_update_monitor_fake_name = 0;
static int cras_alsa_jack_get_name_called;
static const char* cras_alsa_jack_get_name_ret_value = 0;
static char default_jack_name[] = "Something Jack";
static int auto_unplug_input_node_ret = 0;
static int auto_unplug_output_node_ret = 0;
static long cras_alsa_mixer_get_minimum_capture_gain_ret_value;
static long cras_alsa_mixer_get_maximum_capture_gain_ret_value;
static snd_pcm_state_t snd_pcm_state_ret;
static int cras_alsa_attempt_resume_called;
static snd_hctl_t* fake_hctl = (snd_hctl_t*)2;
static size_t ucm_get_dma_period_for_dev_called;
static unsigned int ucm_get_dma_period_for_dev_ret;
static unsigned int cras_volume_curve_create_simple_step_called;
static long cras_volume_curve_create_simple_step_max_volume;
static long cras_volume_curve_create_simple_step_range;
static int cras_card_config_get_volume_curve_for_control_called;
typedef std::map<std::string, struct cras_volume_curve*> VolCurveMap;
static VolCurveMap cras_card_config_get_volume_curve_vals;
static int cras_alsa_mmap_get_whole_buffer_called;
static int cras_iodev_fill_odev_zeros_called;
static unsigned int cras_iodev_fill_odev_zeros_frames;
static int cras_iodev_frames_queued_ret;
static int cras_iodev_buffer_avail_ret;
static int cras_alsa_resume_appl_ptr_called;
static int cras_alsa_resume_appl_ptr_ahead;
static const struct cras_volume_curve* fake_get_dBFS_volume_curve_val;
static int cras_iodev_dsp_set_swap_mode_for_node_called;
static std::map<std::string, long> ucm_get_default_node_gain_values;
static std::map<std::string, long> ucm_get_intrinsic_sensitivity_values;
static thread_callback audio_thread_cb;
static void* audio_thread_cb_data;
static int hotword_send_triggered_msg_called;
static struct timespec clock_gettime_retspec;
static unsigned cras_iodev_reset_rate_estimator_called;
static int sys_aec_on_dsp_supported_return_value;
static int ucm_node_echo_cancellation_exists_ret_value;
static int sys_get_max_internal_speaker_channels_called;
static int sys_get_max_internal_speaker_channels_return_value;
static int sys_get_max_headphone_channels_called = 0;
static int sys_get_max_headphone_channels_return_value = 2;
static int sys_using_default_volume_curve_for_usb_audio_device_value;
static int cras_iodev_update_underrun_duration_called = 0;
static bool testing_channel_retry = false;
static struct cras_board_config fake_board_config;

void cras_dsp_set_variable_integer(struct cras_dsp_context* ctx,
                                   const char* key,
                                   int value) {}

void ResetStubData() {
  cras_alsa_open_called = 0;
  cras_iodev_append_stream_ret = 0;
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = 0;
  cras_alsa_start_called = 0;
  cras_alsa_fill_properties_called = 0;
  cras_alsa_support_8_channels = false;
  sys_get_volume_called = 0;
  alsa_mixer_set_dBFS_called = 0;
  alsa_mixer_set_capture_dBFS_called = 0;
  sys_get_mute_called = 0;
  alsa_mixer_set_mute_called = 0;
  cras_alsa_mixer_get_playback_dBFS_range_called = 0;
  cras_alsa_mixer_get_playback_dBFS_range_max = 0;
  cras_alsa_mixer_get_playback_dBFS_range_min = -2000;
  cras_alsa_mixer_get_playback_step_called = 0;
  cras_alsa_mixer_get_playback_step_values.clear();
  cras_alsa_mixer_get_control_for_section_called = 0;
  cras_alsa_mixer_get_control_for_section_return_value = NULL;
  cras_alsa_mixer_list_outputs_called = 0;
  cras_alsa_mixer_list_outputs_outputs_length = 0;
  cras_alsa_mixer_list_inputs_called = 0;
  cras_alsa_mixer_list_inputs_outputs_length = 0;
  cras_alsa_mixer_set_output_active_state_called = 0;
  cras_alsa_mixer_set_output_active_state_outputs.clear();
  cras_alsa_mixer_set_output_active_state_values.clear();
  sys_set_volume_limits_called = 0;
  cras_alsa_mixer_get_minimum_capture_gain_called = 0;
  cras_alsa_mixer_get_maximum_capture_gain_called = 0;
  cras_alsa_mixer_get_output_volume_curve_called = 0;
  cras_alsa_jack_get_mixer_ret = NULL;
  cras_alsa_mixer_get_control_name_values.clear();
  cras_alsa_mixer_get_control_name_called = 0;
  cras_alsa_jack_list_create_called = 0;
  cras_alsa_jack_list_find_jacks_by_name_matching_called = 0;
  cras_alsa_jack_found_num = 0;
  cras_alsa_jack_list_add_jack_for_section_called = 0;
  cras_alsa_jack_list_add_jack_for_section_result_jack = NULL;
  cras_alsa_jack_list_destroy_called = 0;
  cras_alsa_jack_list_has_hctl_jacks_return_val = 1;
  cras_iodev_add_node_called = 0;
  cras_iodev_set_node_plugged_called = 0;
  cras_alsa_jack_enable_ucm_called = 0;
  ucm_set_enabled_called = 0;
  cras_iodev_update_dsp_called = 0;
  cras_iodev_update_dsp_name = 0;
  ucm_get_dsp_name_for_dev_called = 0;
  ucm_get_dsp_name_for_dev_values.clear();
  cras_iodev_free_resources_called = 0;
  cras_alsa_jack_update_node_type_called = 0;
  ucm_swap_mode_exists_ret_value = 0;
  ucm_enable_swap_mode_ret_value = 0;
  ucm_enable_swap_mode_called = 0;
  is_utf8_string_ret_value = 1;
  cras_alsa_jack_get_name_called = 0;
  cras_alsa_jack_get_name_ret_value = default_jack_name;
  cras_alsa_jack_update_monitor_fake_name = 0;
  cras_card_config_get_volume_curve_for_control_called = 0;
  cras_card_config_get_volume_curve_vals.clear();
  cras_volume_curve_create_simple_step_called = 0;
  cras_alsa_mixer_get_minimum_capture_gain_ret_value = 0;
  cras_alsa_mixer_get_maximum_capture_gain_ret_value = 0;
  snd_pcm_state_ret = SND_PCM_STATE_RUNNING;
  cras_alsa_attempt_resume_called = 0;
  ucm_get_dma_period_for_dev_called = 0;
  ucm_get_dma_period_for_dev_ret = 0;
  cras_alsa_mmap_get_whole_buffer_called = 0;
  cras_iodev_fill_odev_zeros_called = 0;
  cras_iodev_fill_odev_zeros_frames = 0;
  cras_iodev_frames_queued_ret = 0;
  cras_iodev_buffer_avail_ret = 0;
  cras_alsa_resume_appl_ptr_called = 0;
  cras_alsa_resume_appl_ptr_ahead = 0;
  fake_get_dBFS_volume_curve_val = NULL;
  cras_iodev_dsp_set_swap_mode_for_node_called = 0;
  ucm_get_default_node_gain_values.clear();
  ucm_get_intrinsic_sensitivity_values.clear();
  cras_iodev_reset_rate_estimator_called = 0;
  sys_aec_on_dsp_supported_return_value = 0;
  ucm_node_echo_cancellation_exists_ret_value = 0;
  sys_get_max_internal_speaker_channels_called = 0;
  sys_get_max_internal_speaker_channels_return_value = 2;
  sys_get_max_headphone_channels_called = 0;
  sys_get_max_headphone_channels_return_value = 2;
  sys_using_default_volume_curve_for_usb_audio_device_value = 0;
  cras_iodev_update_underrun_duration_called = 0;
  testing_channel_retry = false;
  memset(&fake_board_config, 0, sizeof(fake_board_config));
}

static long fake_get_dBFS(const struct cras_volume_curve* curve,
                          size_t volume) {
  fake_get_dBFS_volume_curve_val = curve;
  return (volume - 100) * 100;
}

static cras_volume_curve default_curve = {
    .get_dBFS = fake_get_dBFS,
};

unsigned int cras_iodev_max_stream_offset(const struct cras_iodev* iodev) {
  return 0;
}

static struct cras_iodev* alsa_iodev_create_with_default_parameters(
    size_t card_index,
    const char* dev_id,
    enum CRAS_ALSA_CARD_TYPE card_type,
    int is_first,
    struct cras_alsa_mixer* mixer,
    struct cras_card_config* config,
    struct cras_use_case_mgr* ucm,
    enum CRAS_STREAM_DIRECTION direction) {
  struct cras_alsa_card_info card_info = {.card_type = card_type,
                                          .card_index = (uint32_t)card_index};
  return alsa_iodev_create(&card_info, test_card_name, 0, test_pcm_name,
                           test_dev_name, dev_id, is_first, mixer, config, ucm,
                           fake_hctl, direction, CRAS_USE_CASE_HIFI, NULL);
}

namespace {
TEST(AlsaIoInit, InitializeInvalidDirection) {
  struct alsa_io* aio;

  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_NUM_DIRECTIONS);
  ASSERT_EQ(aio, (void*)NULL);
}

TEST(AlsaIoInit, InitializePlayback) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, test_dev_id, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  // Get volume curve twice for iodev, and default node.
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->common.alsa_stream);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);
  EXPECT_EQ(0, strncmp(test_card_name, aio->common.base.info.name,
                       strlen(test_card_name)));
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  EXPECT_EQ("", cras_iodev_update_dsp_name);
  ASSERT_NE(reinterpret_cast<const char*>(NULL), aio->common.dev_name);
  EXPECT_EQ(0, strcmp(test_dev_name, aio->common.dev_name));
  ASSERT_NE(reinterpret_cast<const char*>(NULL), aio->common.dev_id);
  EXPECT_EQ(0, strcmp(test_dev_id, aio->common.dev_id));

  alsa_iodev_destroy((struct cras_iodev*)aio);
  EXPECT_EQ(1, cras_iodev_free_resources_called);
}

TEST(AlsaIoInit, DefaultNodeInternalCard) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);
  ASSERT_STREQ(DEFAULT, aio->common.base.active_node->name);
  ASSERT_EQ(1, aio->common.base.active_node->plugged);
  ASSERT_EQ((void*)no_stream, (void*)aio->common.base.no_stream);
  ASSERT_EQ((void*)is_free_running, (void*)aio->common.base.is_free_running);
  alsa_iodev_destroy((struct cras_iodev*)aio);

  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(4, cras_card_config_get_volume_curve_for_control_called);
  ASSERT_STREQ(INTERNAL_SPEAKER, aio->common.base.active_node->name);
  ASSERT_EQ(1, aio->common.base.active_node->plugged);
  ASSERT_EQ((void*)no_stream, (void*)aio->common.base.no_stream);
  ASSERT_EQ((void*)is_free_running, (void*)aio->common.base.is_free_running);
  alsa_iodev_destroy((struct cras_iodev*)aio);

  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_INPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  // No more call to get volume curve for input device.
  EXPECT_EQ(4, cras_card_config_get_volume_curve_for_control_called);
  ASSERT_STREQ(DEFAULT, aio->common.base.active_node->name);
  ASSERT_EQ(1, aio->common.base.active_node->plugged);
  ASSERT_EQ((void*)no_stream, (void*)aio->common.base.no_stream);
  ASSERT_EQ((void*)is_free_running, (void*)aio->common.base.is_free_running);
  alsa_iodev_destroy((struct cras_iodev*)aio);

  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, NULL,
      CRAS_STREAM_INPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(4, cras_card_config_get_volume_curve_for_control_called);
  ASSERT_STREQ(INTERNAL_MICROPHONE, aio->common.base.active_node->name);
  ASSERT_EQ(1, aio->common.base.active_node->plugged);
  ASSERT_EQ((void*)no_stream, (void*)aio->common.base.no_stream);
  ASSERT_EQ((void*)is_free_running, (void*)aio->common.base.is_free_running);
  alsa_iodev_destroy((struct cras_iodev*)aio);
}

TEST(AlsaIoInit, DefaultNodeHDMICard) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_HDMI, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);
  ASSERT_STREQ(DEFAULT, aio->common.base.active_node->name);
  ASSERT_EQ(1, aio->common.base.active_node->plugged);
  ASSERT_EQ((void*)no_stream, (void*)aio->common.base.no_stream);
  ASSERT_EQ((void*)is_free_running, (void*)aio->common.base.is_free_running);
  alsa_iodev_destroy((struct cras_iodev*)aio);

  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_HDMI, 1, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(4, cras_card_config_get_volume_curve_for_control_called);
  ASSERT_STREQ(INTERNAL_SPEAKER, aio->common.base.active_node->name);
  ASSERT_EQ(1, aio->common.base.active_node->plugged);
  ASSERT_EQ((void*)no_stream, (void*)aio->common.base.no_stream);
  ASSERT_EQ((void*)is_free_running, (void*)aio->common.base.is_free_running);
  alsa_iodev_destroy((struct cras_iodev*)aio);

  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_HDMI, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_INPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  // No more call to get volume curve for input device.
  EXPECT_EQ(4, cras_card_config_get_volume_curve_for_control_called);
  ASSERT_STREQ(DEFAULT, aio->common.base.active_node->name);
  ASSERT_EQ(1, aio->common.base.active_node->plugged);
  ASSERT_EQ((void*)no_stream, (void*)aio->common.base.no_stream);
  ASSERT_EQ((void*)is_free_running, (void*)aio->common.base.is_free_running);
  alsa_iodev_destroy((struct cras_iodev*)aio);

  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_HDMI, 1, fake_mixer, fake_config, NULL,
      CRAS_STREAM_INPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(4, cras_card_config_get_volume_curve_for_control_called);
  ASSERT_STREQ(INTERNAL_MICROPHONE, aio->common.base.active_node->name);
  ASSERT_EQ(1, aio->common.base.active_node->plugged);
  ASSERT_EQ((void*)no_stream, (void*)aio->common.base.no_stream);
  ASSERT_EQ((void*)is_free_running, (void*)aio->common.base.is_free_running);
  alsa_iodev_destroy((struct cras_iodev*)aio);
}

TEST(AlsaIoInit, OpenPlayback) {
  struct cras_iodev* iodev;
  struct cras_audio_format format;
  struct alsa_io* aio;

  ResetStubData();
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init(iodev));
  // Call open_dev once on update_max_supported_channels.
  EXPECT_EQ(1, cras_alsa_open_called);
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  aio = (struct alsa_io*)iodev;
  format.frame_rate = 48000;
  format.num_channels = 1;
  cras_iodev_set_format(iodev, &format);

  // Test that these flags are cleared after open_dev.
  aio->common.free_running = 1;
  aio->common.filled_zeros_for_draining = 512;
  iodev->open_dev(iodev);
  EXPECT_EQ(2, cras_alsa_open_called);
  iodev->configure_dev(iodev);
  EXPECT_EQ(2, cras_alsa_open_called);
  EXPECT_EQ(1, sys_set_volume_limits_called);
  EXPECT_EQ(2, alsa_mixer_set_dBFS_called);
  EXPECT_EQ(0, cras_alsa_start_called);
  EXPECT_EQ(0, cras_iodev_set_node_plugged_called);
  EXPECT_EQ(0, aio->common.free_running);
  EXPECT_EQ(0, aio->common.filled_zeros_for_draining);
  EXPECT_EQ(SEVERE_UNDERRUN_MS * format.frame_rate / 1000,
            aio->common.severe_underrun_frames);
  iodev->close_dev(iodev);
  alsa_iodev_destroy(iodev);
  free(fake_format);
}

TEST(AlsaIoInit, QuadChannelInternalSpeakerOpenPlayback) {
  struct cras_iodev* iodev;
  struct cras_audio_format format;

  ResetStubData();
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init(iodev));
  format.frame_rate = 48000;
  format.num_channels = 4;
  cras_iodev_set_format(iodev, &format);
  iodev->active_node->type = CRAS_NODE_TYPE_INTERNAL_SPEAKER;
  iodev->open_dev(iodev);
  iodev->configure_dev(iodev);

  iodev->close_dev(iodev);
  alsa_iodev_destroy(iodev);
  free(fake_format);
}

TEST(AlsaIoInit, RetryHWParamWithStereo) {
  struct cras_iodev* iodev;
  struct cras_audio_format format;

  ResetStubData();
  testing_channel_retry = true;
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init(iodev));
  format.frame_rate = 48000;
  format.num_channels = 6;
  cras_iodev_set_format(iodev, &format);
  EXPECT_EQ(6, iodev->format->num_channels);
  iodev->active_node->type = CRAS_NODE_TYPE_INTERNAL_SPEAKER;
  iodev->open_dev(iodev);
  iodev->configure_dev(iodev);

  EXPECT_EQ(2, iodev->format->num_channels);
  iodev->close_dev(iodev);
  alsa_iodev_destroy(iodev);
  free(fake_format);
}

TEST(AlsaIoInit, SoftwareGainIntrinsicSensitivity) {
  struct cras_iodev* iodev;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  long intrinsic_sensitivity = -2700;

  ResetStubData();

  // Set intrinsic sensitivity to -2700 * 0.01 dBFS/Pa.
  ucm_get_intrinsic_sensitivity_values[INTERNAL_MICROPHONE] =
      intrinsic_sensitivity;

  // Assume this is the first device so it gets internal mic node name.
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_INPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init(iodev));
  ASSERT_EQ(intrinsic_sensitivity, iodev->active_node->intrinsic_sensitivity);
  ASSERT_EQ(DEFAULT_CAPTURE_VOLUME_DBFS - intrinsic_sensitivity,
            iodev->active_node->internal_capture_gain);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaIoInit, RouteBasedOnJackCallback) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  const struct cras_alsa_jack* jack = (struct cras_alsa_jack*)4;

  ResetStubData();
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void*)NULL);

  // Add a node with jack
  cras_alsa_jack_found_num = 1;
  cras_alsa_jack_found_val[0] = jack;

  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  // 1 for iodev creation + 1 for node creation + 2 for jack assignment
  EXPECT_EQ(4, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->common.alsa_stream);
  EXPECT_EQ(1, cras_alsa_jack_list_create_called);
  EXPECT_EQ(1, cras_alsa_jack_list_find_jacks_by_name_matching_called);
  EXPECT_EQ(0, cras_alsa_jack_list_add_jack_for_section_called);

  jack_plug_cb(jack, 1, jack_plug_cb_data);
  EXPECT_EQ(1, cras_iodev_set_node_plugged_called);
  EXPECT_EQ(1, cras_iodev_set_node_plugged_value);
  jack_plug_cb(jack, 0, jack_plug_cb_data);
  EXPECT_EQ(2, cras_iodev_set_node_plugged_called);
  EXPECT_EQ(0, cras_iodev_set_node_plugged_value);

  alsa_iodev_destroy((struct cras_iodev*)aio);
  EXPECT_EQ(1, cras_alsa_jack_list_destroy_called);
}

TEST(AlsaIoInit, RouteBasedOnInputJackCallback) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  const struct cras_alsa_jack* jack = (struct cras_alsa_jack*)4;

  ResetStubData();
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_INPUT);
  ASSERT_NE(aio, (void*)NULL);

  // Add a node with jack
  cras_alsa_jack_found_num = 1;
  cras_alsa_jack_found_val[0] = jack;

  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));

  EXPECT_EQ(SND_PCM_STREAM_CAPTURE, aio->common.alsa_stream);
  EXPECT_EQ(1, cras_alsa_jack_list_create_called);
  EXPECT_EQ(1, cras_alsa_jack_list_find_jacks_by_name_matching_called);
  EXPECT_EQ(0, cras_alsa_jack_list_add_jack_for_section_called);

  jack_plug_cb(jack, 1, jack_plug_cb_data);
  EXPECT_EQ(1, cras_iodev_set_node_plugged_called);
  EXPECT_EQ(1, cras_iodev_set_node_plugged_value);
  jack_plug_cb(jack, 0, jack_plug_cb_data);
  EXPECT_EQ(2, cras_iodev_set_node_plugged_called);
  EXPECT_EQ(0, cras_iodev_set_node_plugged_value);

  alsa_iodev_destroy((struct cras_iodev*)aio);
  EXPECT_EQ(1, cras_alsa_jack_list_destroy_called);
}

TEST(AlsaIoInit, InitializeCapture) {
  struct alsa_io* aio;

  ResetStubData();
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, NULL,
      CRAS_STREAM_INPUT);
  ASSERT_NE(aio, (void*)NULL);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));

  EXPECT_EQ(SND_PCM_STREAM_CAPTURE, aio->common.alsa_stream);
  // Call cras_alsa_fill_properties once on update_max_supported_channels.
  EXPECT_EQ(1, cras_alsa_fill_properties_called);
  EXPECT_EQ(1, cras_alsa_mixer_list_inputs_called);

  alsa_iodev_destroy((struct cras_iodev*)aio);
}

TEST(AlsaIoInit, OpenCapture) {
  struct cras_iodev* iodev;
  struct cras_audio_format format;
  struct alsa_io* aio;

  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_INPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init(iodev));

  aio = (struct alsa_io*)iodev;
  format.frame_rate = 48000;
  format.num_channels = 1;
  cras_iodev_set_format(iodev, &format);

  ResetStubData();
  iodev->open_dev(iodev);
  EXPECT_EQ(1, cras_alsa_open_called);
  iodev->configure_dev(iodev);
  EXPECT_EQ(1, cras_alsa_open_called);
  EXPECT_EQ(1, cras_alsa_mixer_get_minimum_capture_gain_called);
  EXPECT_EQ(1, cras_alsa_mixer_get_maximum_capture_gain_called);
  EXPECT_EQ(1, alsa_mixer_set_capture_dBFS_called);
  EXPECT_EQ(1, cras_alsa_start_called);
  EXPECT_EQ(SEVERE_UNDERRUN_MS * format.frame_rate / 1000,
            aio->common.severe_underrun_frames);
  iodev->close_dev(iodev);
  alsa_iodev_destroy(iodev);
  free(fake_format);
}

TEST(AlsaIoInit, OpenCaptureSetCaptureGainWithDefaultNodeGain) {
  struct cras_iodev* iodev;
  struct cras_audio_format format;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  long default_node_gain = 1000;

  ResetStubData();
  // Set default node gain to -1000 * 0.01 dB.
  ucm_get_default_node_gain_values[INTERNAL_MICROPHONE] = default_node_gain;

  // Assume this is the first device so it gets internal mic node name.
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_INPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init(iodev));

  format.frame_rate = 48000;
  format.num_channels = 1;
  cras_iodev_set_format(iodev, &format);

  // Check the default node gain is the same as what specified in UCM.
  EXPECT_EQ(default_node_gain, iodev->active_node->internal_capture_gain);
  cras_alsa_mixer_get_minimum_capture_gain_ret_value = 0;
  cras_alsa_mixer_get_maximum_capture_gain_ret_value = 2000;

  iodev->open_dev(iodev);
  iodev->configure_dev(iodev);
  iodev->close_dev(iodev);

  // Hardware gain is in the hardware gain range and set to 1000 * 0.01 dB.
  EXPECT_EQ(default_node_gain, alsa_mixer_set_capture_dBFS_value);

  // Check we do respect the hardware maximum capture gain.
  cras_alsa_mixer_get_maximum_capture_gain_ret_value = 500;

  iodev->open_dev(iodev);
  iodev->configure_dev(iodev);
  iodev->close_dev(iodev);

  EXPECT_EQ(500, alsa_mixer_set_capture_dBFS_value);

  alsa_iodev_destroy(iodev);
  free(fake_format);
}

TEST(AlsaIoInit, OpenCaptureSetCaptureGainWithSoftwareGain) {
  struct cras_iodev* iodev;
  struct cras_audio_format format;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;

  // Meet the requirements of using software gain.
  ResetStubData();

  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_INPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init(iodev));

  format.frame_rate = 48000;
  format.num_channels = 1;
  cras_iodev_set_format(iodev, &format);

  iodev->open_dev(iodev);
  iodev->configure_dev(iodev);
  iodev->close_dev(iodev);

  // Hardware gain is set to 0dB when software gain is used.
  EXPECT_EQ(0, alsa_mixer_set_capture_dBFS_value);

  // Test the case where software gain is not needed.
  iodev->active_node->software_volume_needed = 0;
  iodev->active_node->internal_capture_gain = 1000;
  iodev->open_dev(iodev);
  iodev->configure_dev(iodev);
  iodev->close_dev(iodev);

  // Hardware gain is set to 1000 * 0.01 dB as got from catpure_gain.
  EXPECT_EQ(0, alsa_mixer_set_capture_dBFS_value);

  alsa_iodev_destroy(iodev);
  free(fake_format);
}

TEST(AlsaIoInit, UpdateActiveNode) {
  struct cras_iodev* iodev;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init(iodev));
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);

  iodev->update_active_node(iodev, 0, 1);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaIoInit, StartDevice) {
  struct cras_iodev* iodev;
  int rc;

  ResetStubData();
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, NULL, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init(iodev));
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);

  // Return right away if it is already running.
  snd_pcm_state_ret = SND_PCM_STATE_RUNNING;
  rc = iodev->start(iodev);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_alsa_start_called);

  // Otherwise, start the device.
  snd_pcm_state_ret = SND_PCM_STATE_SETUP;
  rc = iodev->start(iodev);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_alsa_start_called);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaIoInit, ResumeDevice) {
  struct cras_iodev* iodev;
  int rc;

  ResetStubData();
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, NULL, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init(iodev));
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);

  // Attempt to resume if the device is suspended.
  snd_pcm_state_ret = SND_PCM_STATE_SUSPENDED;
  rc = iodev->start(iodev);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_alsa_attempt_resume_called);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaIoInit, DspNameDefault) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;

  ResetStubData();
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->common.alsa_stream);
  EXPECT_EQ(1, ucm_get_dsp_name_for_dev_called);
  EXPECT_STREQ("", cras_iodev_update_dsp_name);

  alsa_iodev_destroy((struct cras_iodev*)aio);
}

TEST(AlsaIoInit, DspName) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;

  ResetStubData();
  ucm_get_dsp_name_for_dev_values[DEFAULT] = "hello";
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->common.alsa_stream);
  EXPECT_EQ(1, ucm_get_dsp_name_for_dev_called);
  EXPECT_STREQ("hello", cras_iodev_update_dsp_name);

  alsa_iodev_destroy((struct cras_iodev*)aio);
}

TEST(AlsaIoInit, DspNameJackOverride) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  const struct cras_alsa_jack* jack = (struct cras_alsa_jack*)4;
  static const char* jack_name = "jack";
  struct mixer_control* spk = reinterpret_cast<struct mixer_control*>(5);

  ResetStubData();
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);

  // Add spk node in mixer.
  cras_alsa_mixer_list_outputs_outputs = &spk;
  cras_alsa_mixer_list_outputs_outputs_length = 1;
  cras_alsa_mixer_get_control_name_values[spk] = INTERNAL_SPEAKER;

  cras_alsa_jack_get_name_ret_value = jack_name;
  ucm_get_dsp_name_for_dev_values[jack_name] = "override_dsp";
  // Add the jack node.
  cras_alsa_jack_found_num = 1;
  cras_alsa_jack_found_val[0] = jack;

  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->common.alsa_stream);
  EXPECT_EQ(2, ucm_get_dsp_name_for_dev_called);
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  EXPECT_STREQ("", cras_iodev_update_dsp_name);

  EXPECT_EQ(2, cras_alsa_jack_get_name_called);

  // Mark the jack node as active.
  alsa_iodev_set_active_node(&aio->common.base, aio->common.base.nodes->next,
                             1);
  EXPECT_EQ(2, ucm_get_dsp_name_for_dev_called);
  EXPECT_EQ(2, cras_iodev_update_dsp_called);

  // Mark the default node as active.
  alsa_iodev_set_active_node(&aio->common.base, aio->common.base.nodes, 1);
  EXPECT_EQ(2, ucm_get_dsp_name_for_dev_called);
  EXPECT_EQ(3, cras_iodev_update_dsp_called);
  EXPECT_STREQ("", cras_iodev_update_dsp_name);

  alsa_iodev_destroy((struct cras_iodev*)aio);
}

TEST(AlsaIoInit, NodeTypeOverride) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;

  ResetStubData();
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);

  // Add the jack node.
  cras_alsa_jack_found_num = 1;
  cras_alsa_jack_found_val[0] = (struct cras_alsa_jack*)4;
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));

  // Verify that cras_alsa_jack_update_node_type is called when an output device
  // is created.
  EXPECT_EQ(1, cras_alsa_jack_update_node_type_called);

  alsa_iodev_destroy((struct cras_iodev*)aio);
}

TEST(AlsaIoInit, SwapMode) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_ionode* const fake_node =
      (cras_ionode*)calloc(1, sizeof(struct cras_ionode));
  ResetStubData();
  // Stub replies that swap mode does not exist.
  ucm_swap_mode_exists_ret_value = 0;

  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));

  aio->common.base.set_swap_mode_for_node((cras_iodev*)aio, fake_node, 1);
  // Swap mode is implemented by dsp.
  EXPECT_EQ(1, cras_iodev_dsp_set_swap_mode_for_node_called);

  // Stub replies that swap mode exists.
  ucm_swap_mode_exists_ret_value = 1;
  alsa_iodev_destroy((struct cras_iodev*)aio);

  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  // Enable swap mode.
  aio->common.base.set_swap_mode_for_node((cras_iodev*)aio, fake_node, 1);

  // Verify that ucm_enable_swap_mode is called when callback to enable
  // swap mode is called.
  EXPECT_EQ(1, ucm_enable_swap_mode_called);

  alsa_iodev_destroy((struct cras_iodev*)aio);
  free(fake_node);
}

TEST(AlsaIoInit, MaxSupportedChannelsInternalSpeaker) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  int i;

  // i = 0: cras_alsa_support_8_channels is false, support 2 channels only.
  // i = 1: cras_alsa_support_8_channels is true, support up to 8 channels.
  for (i = 0; i < 2; i++) {
    ResetStubData();
    cras_alsa_support_8_channels = (bool)i;
    sys_get_max_internal_speaker_channels_return_value = i * 2;

    aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
        0, test_dev_id, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config,
        NULL, CRAS_STREAM_OUTPUT);
    ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
    /* No need to call cras_alsa_fill_properties_called for the internal
     * speaker. */
    EXPECT_EQ(0, cras_alsa_fill_properties_called);
    EXPECT_EQ(1, sys_get_max_internal_speaker_channels_called);
    EXPECT_EQ(i * 2, sys_get_max_internal_speaker_channels_return_value);
    EXPECT_EQ(sys_get_max_internal_speaker_channels_return_value,
              aio->common.base.info.max_supported_channels);
    alsa_iodev_destroy((struct cras_iodev*)aio);
    EXPECT_EQ(1, cras_iodev_free_resources_called);
  }
}
// Test that system settings aren't touched if no streams active.
TEST(AlsaOutputNode, SystemSettingsWhenInactive) {
  int rc;
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct mixer_control* outputs[2];

  ResetStubData();
  outputs[0] = reinterpret_cast<struct mixer_control*>(3);
  outputs[1] = reinterpret_cast<struct mixer_control*>(4);
  cras_alsa_mixer_list_outputs_outputs = outputs;
  cras_alsa_mixer_list_outputs_outputs_length = ARRAY_SIZE(outputs);
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  // Two mixer controls calls get volume curve.
  EXPECT_EQ(4, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->common.alsa_stream);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);

  ResetStubData();
  rc = alsa_iodev_set_active_node((struct cras_iodev*)aio,
                                  aio->common.base.nodes->next, 1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, alsa_mixer_set_mute_called);
  // Do set the volume even if no stream
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  ASSERT_EQ(2, cras_alsa_mixer_set_output_active_state_called);
  EXPECT_EQ(outputs[0], cras_alsa_mixer_set_output_active_state_outputs[0]);
  EXPECT_EQ(0, cras_alsa_mixer_set_output_active_state_values[0]);
  EXPECT_EQ(outputs[1], cras_alsa_mixer_set_output_active_state_outputs[1]);
  EXPECT_EQ(1, cras_alsa_mixer_set_output_active_state_values[1]);
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  // No jack is defined, and UCM is not used.
  EXPECT_EQ(0, cras_alsa_jack_enable_ucm_called);
  EXPECT_EQ(0, ucm_set_enabled_called);

  alsa_iodev_destroy((struct cras_iodev*)aio);
}

//  Test handling of different amounts of outputs.
TEST(AlsaOutputNode, TwoOutputs) {
  int rc;
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct mixer_control* outputs[2];

  ResetStubData();
  outputs[0] = reinterpret_cast<struct mixer_control*>(3);
  outputs[1] = reinterpret_cast<struct mixer_control*>(4);
  cras_alsa_mixer_list_outputs_outputs = outputs;
  cras_alsa_mixer_list_outputs_outputs_length = ARRAY_SIZE(outputs);
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(4, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->common.alsa_stream);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);

  aio->common.handle = (snd_pcm_t*)0x24;

  ResetStubData();
  rc = alsa_iodev_set_active_node((struct cras_iodev*)aio,
                                  aio->common.base.nodes->next, 1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, alsa_mixer_set_mute_called);
  EXPECT_EQ(outputs[1], alsa_mixer_set_mute_output);
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  EXPECT_EQ(outputs[1], alsa_mixer_set_dBFS_output);
  ASSERT_EQ(2, cras_alsa_mixer_set_output_active_state_called);
  EXPECT_EQ(outputs[0], cras_alsa_mixer_set_output_active_state_outputs[0]);
  EXPECT_EQ(0, cras_alsa_mixer_set_output_active_state_values[0]);
  EXPECT_EQ(outputs[1], cras_alsa_mixer_set_output_active_state_outputs[1]);
  EXPECT_EQ(1, cras_alsa_mixer_set_output_active_state_values[1]);
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  // No jacks defined, and UCM is not used.
  EXPECT_EQ(0, cras_alsa_jack_enable_ucm_called);
  EXPECT_EQ(0, ucm_set_enabled_called);

  alsa_iodev_destroy((struct cras_iodev*)aio);
}

TEST(AlsaOutputNode, TwoJacksHeadphoneLineout) {
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  struct mixer_control* output;
  struct ucm_section* section;

  ResetStubData();
  output = reinterpret_cast<struct mixer_control*>(3);
  cras_alsa_mixer_get_control_name_values[output] = HEADPHONE;

  // Create the iodev
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);
  ASSERT_NE(iodev, (void*)NULL);
  EXPECT_EQ(1, cras_card_config_get_volume_curve_for_control_called);

  // First node 'Headphone'
  section = ucm_section_create(HEADPHONE, "hw:0,1", 0, -1, CRAS_STREAM_OUTPUT,
                               "fake-jack", "gpio");
  ucm_section_set_mixer_name(section, HEADPHONE);
  cras_alsa_jack_list_add_jack_for_section_result_jack =
      reinterpret_cast<struct cras_alsa_jack*>(10);
  cras_alsa_mixer_get_control_for_section_return_value = output;
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  ucm_section_free_list(section);

  // Second node 'Line Out'
  section = ucm_section_create("Line Out", "hw:0.1", 0, -1, CRAS_STREAM_OUTPUT,
                               "fake-jack", "gpio");
  ucm_section_set_mixer_name(section, HEADPHONE);
  cras_alsa_jack_list_add_jack_for_section_result_jack =
      reinterpret_cast<struct cras_alsa_jack*>(20);
  cras_alsa_mixer_get_control_for_section_return_value = output;
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  ucm_section_free_list(section);

  //
  alsa_iodev_ucm_complete_init(iodev);
  EXPECT_EQ(7, cras_card_config_get_volume_curve_for_control_called);

  // Both nodes are associated with the same mixer output. Different jack plug
  // report should trigger different node attribute change.
  cras_alsa_jack_get_mixer_ret = output;
  jack_plug_cb(reinterpret_cast<struct cras_alsa_jack*>(10), 0,
               jack_plug_cb_data);
  EXPECT_STREQ(cras_iodev_set_node_plugged_ionode->name, HEADPHONE);

  jack_plug_cb(reinterpret_cast<struct cras_alsa_jack*>(20), 0,
               jack_plug_cb_data);
  EXPECT_STREQ(cras_iodev_set_node_plugged_ionode->name, "Line Out");

  alsa_iodev_destroy(iodev);
}

TEST(AlsaOutputNode, MaxSupportedChannelsInternalSpeaker) {
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  struct ucm_section* section;
  int i;

  // i = 0: cras_alsa_support_8_channels is false, support 2 channels only.
  // i = 1: cras_alsa_support_8_channels is true, support up to 8 channels.
  for (i = 0; i < 2; i++) {
    ResetStubData();
    cras_alsa_support_8_channels = (bool)i;

    // Create the IO device.
    iodev = alsa_iodev_create_with_default_parameters(
        1, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
        CRAS_STREAM_OUTPUT);
    ASSERT_NE(iodev, (void*)NULL);

    // Node without controls or jacks.
    section = ucm_section_create(INTERNAL_SPEAKER, "hw:0,1", 1, -1,
                                 CRAS_STREAM_OUTPUT, NULL, NULL);
    // Device index doesn't match.
    EXPECT_EQ(-22, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
    section->dev_idx = 0;
    ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
    ucm_section_free_list(section);

    // Complete initialization, and make first node active.
    alsa_iodev_ucm_complete_init(iodev);
    /* No need to call cras_alsa_fill_properties_called for the internal
     * speaker. */
    EXPECT_EQ(0, cras_alsa_fill_properties_called);
    // Always expose internal speaker as a stereo device.
    EXPECT_EQ(2, iodev->info.max_supported_channels);
    alsa_iodev_destroy(iodev);
  }
}

TEST(AlsaOutputNode, OutputsFromUCM) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  static const char* jack_name = "TestCard - Headset Jack";
  struct mixer_control* outputs[2];
  int rc;
  struct ucm_section* section;
  struct cras_alsa_jack* hp_jack;

  ResetStubData();
  outputs[0] = reinterpret_cast<struct mixer_control*>(3);
  outputs[1] = reinterpret_cast<struct mixer_control*>(4);
  cras_alsa_mixer_list_outputs_outputs = outputs;
  cras_alsa_mixer_list_outputs_outputs_length = ARRAY_SIZE(outputs);
  cras_alsa_mixer_get_control_name_values[outputs[0]] = INTERNAL_SPEAKER;
  cras_alsa_mixer_get_control_name_values[outputs[1]] = HEADPHONE;
  ucm_get_dma_period_for_dev_ret = 1000;

  // Create the IO device.
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);
  ASSERT_NE(iodev, (void*)NULL);
  aio = reinterpret_cast<struct alsa_io*>(iodev);
  EXPECT_EQ(1, cras_card_config_get_volume_curve_for_control_called);

  // First node.
  section = ucm_section_create(INTERNAL_SPEAKER, "hw:0,1", 0, -1,
                               CRAS_STREAM_OUTPUT, NULL, NULL);
  ucm_section_set_mixer_name(section, INTERNAL_SPEAKER);
  cras_alsa_jack_list_add_jack_for_section_result_jack =
      reinterpret_cast<struct cras_alsa_jack*>(1);
  cras_alsa_mixer_get_control_for_section_return_value = outputs[0];
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  ucm_section_free_list(section);

  // Add a second node (will use the same iodev).
  section = ucm_section_create(HEADPHONE, "hw:0,2", 0, -1, CRAS_STREAM_OUTPUT,
                               jack_name, "hctl");
  ucm_section_add_coupled(section, "HP-L", MIXER_NAME_VOLUME);
  ucm_section_add_coupled(section, "HP-R", MIXER_NAME_VOLUME);
  hp_jack = reinterpret_cast<struct cras_alsa_jack*>(0x123);
  cras_alsa_jack_list_add_jack_for_section_result_jack = hp_jack;
  cras_alsa_mixer_get_control_for_section_return_value = outputs[1];
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  ucm_section_free_list(section);

  // Jack plug of an unknown device should do nothing.
  cras_alsa_jack_get_mixer_ret = NULL;
  cras_alsa_jack_get_name_ret_value = "Some other jack";
  jack_plug_cb(reinterpret_cast<struct cras_alsa_jack*>(4), 0,
               jack_plug_cb_data);
  EXPECT_EQ(0, cras_iodev_set_node_plugged_called);

  // Complete initialization, and make first node active.
  cras_alsa_support_8_channels = false;  // Support 2 channels only.
  alsa_iodev_ucm_complete_init(iodev);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->common.alsa_stream);
  EXPECT_EQ(7, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(2, cras_alsa_jack_list_add_jack_for_section_called);
  EXPECT_EQ(2, cras_alsa_mixer_get_control_for_section_called);
  EXPECT_EQ(1, ucm_get_dma_period_for_dev_called);
  EXPECT_EQ(ucm_get_dma_period_for_dev_ret,
            aio->common.dma_period_set_microsecs);
  EXPECT_EQ(2, iodev->info.max_supported_channels);

  aio->common.handle = (snd_pcm_t*)0x24;

  // Simulate jack plug event.
  cras_alsa_support_8_channels = true;  // Support up to 8 channels.
  cras_alsa_jack_get_mixer_ret = outputs[1];
  cras_alsa_jack_get_name_ret_value = jack_name;
  jack_plug_cb(hp_jack, 0, jack_plug_cb_data);
  EXPECT_EQ(1, cras_iodev_set_node_plugged_called);
  // Headphone plug event shouldn't trigger update_max_supported_channels.
  EXPECT_EQ(0, cras_alsa_fill_properties_called);
  EXPECT_EQ(2, iodev->info.max_supported_channels);

  // Headphone is not plugged, set it as active.
  ResetStubData();
  rc = alsa_iodev_set_active_node(iodev, aio->common.base.nodes->next, 1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, alsa_mixer_set_mute_called);
  EXPECT_EQ(outputs[1], alsa_mixer_set_mute_output);
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  EXPECT_EQ(outputs[1], alsa_mixer_set_dBFS_output);
  ASSERT_EQ(2, cras_alsa_mixer_set_output_active_state_called);
  EXPECT_EQ(outputs[0], cras_alsa_mixer_set_output_active_state_outputs[0]);
  EXPECT_EQ(0, cras_alsa_mixer_set_output_active_state_values[0]);
  EXPECT_EQ(outputs[1], cras_alsa_mixer_set_output_active_state_outputs[1]);
  EXPECT_EQ(1, cras_alsa_mixer_set_output_active_state_values[1]);
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  EXPECT_EQ(2, cras_alsa_jack_enable_ucm_called);
  EXPECT_EQ(0, ucm_set_enabled_called);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaOutputNode, OutputNoControlsUCM) {
  struct alsa_io* aio;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  struct ucm_section* section;

  ResetStubData();

  // Create the IO device.
  iodev = alsa_iodev_create_with_default_parameters(
      1, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);
  ASSERT_NE(iodev, (void*)NULL);
  aio = reinterpret_cast<struct alsa_io*>(iodev);
  EXPECT_EQ(1, cras_card_config_get_volume_curve_for_control_called);

  // Node without controls or jacks.
  section = ucm_section_create(INTERNAL_SPEAKER, "hw:0,1", 1, -1,
                               CRAS_STREAM_OUTPUT, NULL, NULL);
  // Device index doesn't match.
  EXPECT_EQ(-22, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  section->dev_idx = 0;
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  EXPECT_EQ(1, cras_alsa_mixer_get_control_for_section_called);
  EXPECT_EQ(1, cras_iodev_add_node_called);
  ucm_section_free_list(section);

  // Complete initialization, and make first node active.
  alsa_iodev_ucm_complete_init(iodev);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->common.alsa_stream);
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(0, cras_alsa_mixer_get_control_name_called);
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  EXPECT_EQ(0, cras_alsa_jack_enable_ucm_called);
  EXPECT_EQ(1, ucm_set_enabled_called);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaOutputNode, OutputFromJackUCM) {
  struct alsa_io* aio;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  static const char* jack_name = "TestCard - Headset Jack";
  struct ucm_section* section;

  ResetStubData();

  // Create the IO device.
  iodev = alsa_iodev_create_with_default_parameters(
      1, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);
  ASSERT_NE(iodev, (void*)NULL);
  aio = reinterpret_cast<struct alsa_io*>(iodev);
  EXPECT_EQ(1, cras_card_config_get_volume_curve_for_control_called);

  // Node without controls or jacks.
  cras_alsa_jack_list_add_jack_for_section_result_jack =
      reinterpret_cast<struct cras_alsa_jack*>(1);
  section = ucm_section_create(HEADPHONE, "hw:0,1", 0, -1, CRAS_STREAM_OUTPUT,
                               jack_name, "hctl");
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  EXPECT_EQ(1, cras_alsa_mixer_get_control_for_section_called);
  EXPECT_EQ(1, cras_iodev_add_node_called);
  EXPECT_EQ(1, cras_alsa_jack_list_add_jack_for_section_called);
  ucm_section_free_list(section);

  // Complete initialization, and make first node active.
  alsa_iodev_ucm_complete_init(iodev);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->common.alsa_stream);
  EXPECT_EQ(4, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(0, cras_alsa_mixer_get_control_name_called);
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  EXPECT_EQ(1, cras_alsa_jack_enable_ucm_called);
  EXPECT_EQ(0, ucm_set_enabled_called);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaOutputNode, DspOffloadMapForOutputs) {
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  struct ucm_section* section;

  // Create the IO device.
  ResetStubData();
  iodev = alsa_iodev_create_with_default_parameters(
      1, nullptr, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);
  ASSERT_NE(iodev, nullptr);

  // Add INTERNAL_SPEAKER node without controls or jacks.
  section = ucm_section_create(INTERNAL_SPEAKER, "hw:0,1", 1, -1,
                               CRAS_STREAM_OUTPUT, nullptr, nullptr);
  // Device index doesn't match.
  EXPECT_EQ(-22, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  section->dev_idx = 0;
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  ucm_section_free_list(section);

  // Complete initialization.
  alsa_iodev_ucm_complete_init(iodev);
  // dsp_offload_map is allocated for IO device with INTERNAL_SPEAKER node.
  EXPECT_NE(iodev->dsp_offload_map, nullptr);
  alsa_iodev_destroy(iodev);
  // dsp_offload_map is released by cras_iodev_free_resources().
  EXPECT_EQ(1, cras_iodev_free_resources_called);

  // Create the IO device.
  ResetStubData();
  iodev = alsa_iodev_create_with_default_parameters(
      1, nullptr, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);
  ASSERT_NE(iodev, nullptr);

  // Add LineOut node without controls or jacks.
  section = ucm_section_create("Line Out", "hw:0,1", 1, -1, CRAS_STREAM_OUTPUT,
                               nullptr, nullptr);
  // Device index doesn't match.
  EXPECT_EQ(-22, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  section->dev_idx = 0;
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  ucm_section_free_list(section);

  // Complete initialization.
  alsa_iodev_ucm_complete_init(iodev);
  // dsp_offload_map should not be allocated.
  EXPECT_EQ(iodev->dsp_offload_map, nullptr);
  alsa_iodev_destroy(iodev);
}

TEST(AlsaOutputNode, InputsFromUCM) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct mixer_control* inputs[2];
  struct cras_iodev* iodev;
  static const char* jack_name = "TestCard - Headset Jack";
  int rc;
  struct ucm_section* section;
  long intrinsic_sensitivity = -2700;

  ResetStubData();
  inputs[0] = reinterpret_cast<struct mixer_control*>(3);
  inputs[1] = reinterpret_cast<struct mixer_control*>(4);
  cras_alsa_mixer_list_inputs_outputs = inputs;
  cras_alsa_mixer_list_inputs_outputs_length = ARRAY_SIZE(inputs);
  cras_alsa_mixer_get_control_name_values[inputs[0]] = INTERNAL_MICROPHONE;
  cras_alsa_mixer_get_control_name_values[inputs[1]] = MIC;

  // Create the IO device.
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_INPUT);
  ASSERT_NE(iodev, (void*)NULL);
  aio = reinterpret_cast<struct alsa_io*>(iodev);

  // First node.
  cras_alsa_mixer_get_control_for_section_return_value = inputs[0];
  section = ucm_section_create(INTERNAL_MICROPHONE, "hw:0,1", 0, -1,
                               CRAS_STREAM_INPUT, NULL, NULL);
  ucm_section_add_coupled(section, "MIC-L", MIXER_NAME_VOLUME);
  ucm_section_add_coupled(section, "MIC-R", MIXER_NAME_VOLUME);
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  ASSERT_NE(iodev->nodes, (void*)NULL);
  ucm_section_free_list(section);

  // Add a second node (will use the same iodev).
  cras_alsa_mixer_get_control_name_called = 0;
  // Set intrinsic sensitivity to enable software gain.
  ucm_get_intrinsic_sensitivity_values[MIC] = intrinsic_sensitivity;
  struct cras_alsa_jack* mic_jack = reinterpret_cast<struct cras_alsa_jack*>(1);
  cras_alsa_jack_list_add_jack_for_section_result_jack = mic_jack;
  cras_alsa_mixer_get_control_for_section_return_value = inputs[1];
  section = ucm_section_create(MIC, "hw:0,2", 0, -1, CRAS_STREAM_INPUT,
                               jack_name, "hctl");
  ucm_section_set_mixer_name(section, MIC);
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  ucm_section_free_list(section);

  // Jack plug of an unknown device should do nothing.
  cras_alsa_jack_get_mixer_ret = NULL;
  cras_alsa_jack_get_name_ret_value = "Some other jack";
  jack_plug_cb(reinterpret_cast<struct cras_alsa_jack*>(4), 0,
               jack_plug_cb_data);
  EXPECT_EQ(0, cras_iodev_set_node_plugged_called);

  // Simulate jack plug event.
  cras_alsa_jack_get_mixer_ret = inputs[1];
  cras_alsa_jack_get_name_ret_value = jack_name;
  jack_plug_cb(mic_jack, 0, jack_plug_cb_data);
  EXPECT_EQ(1, cras_iodev_set_node_plugged_called);

  // Complete initialization, and make first node active.
  alsa_iodev_ucm_complete_init(iodev);
  EXPECT_EQ(SND_PCM_STREAM_CAPTURE, aio->common.alsa_stream);
  EXPECT_EQ(2, cras_alsa_jack_list_add_jack_for_section_called);
  EXPECT_EQ(2, cras_alsa_mixer_get_control_for_section_called);
  EXPECT_EQ(1, cras_alsa_mixer_get_control_name_called);
  EXPECT_EQ(2, cras_iodev_add_node_called);
  EXPECT_EQ(2, ucm_get_dma_period_for_dev_called);
  EXPECT_EQ(0, aio->common.dma_period_set_microsecs);

  aio->common.handle = (snd_pcm_t*)0x24;

  ResetStubData();
  rc = alsa_iodev_set_active_node(iodev, aio->common.base.nodes->next, 1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, alsa_mixer_set_capture_dBFS_called);
  EXPECT_EQ(inputs[1], alsa_mixer_set_capture_dBFS_input);
  EXPECT_EQ(0, alsa_mixer_set_capture_dBFS_value);
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  EXPECT_EQ(1, cras_alsa_jack_enable_ucm_called);
  EXPECT_EQ(1, ucm_set_enabled_called);
  ASSERT_EQ(DEFAULT_CAPTURE_VOLUME_DBFS - intrinsic_sensitivity,
            iodev->active_node->internal_capture_gain);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaOutputNode, InputNoiseCancellationSupport) {
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  struct ucm_section* section;
  int i;

  // i = 0: cras_system_get_noise_cancellation_supported is false
  // i = 1: cras_system_get_noise_cancellation_supported is true
  for (i = 0; i < 2; i++) {
    ResetStubData();
    cras_s2_set_dsp_nc_supported((bool)i);

    // Create the IO device for Internal Microphone.
    iodev = alsa_iodev_create_with_default_parameters(
        0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
        CRAS_STREAM_INPUT);
    ASSERT_NE(iodev, (void*)NULL);

    // Create the node.
    section = ucm_section_create(INTERNAL_MICROPHONE, "hw:0,1", 0, -1,
                                 CRAS_STREAM_INPUT, NULL, NULL);
    ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
    ASSERT_NE(iodev->nodes, (void*)NULL);

    // ucm_node_noise_cancellation_exists is 1 for Internal Microphone.
    // However node.nc_providers will have CRAS_NC_PROVIDER_DSP on only if
    // cras_system_get_noise_cancellation_supported is true.
    ASSERT_EQ(
        cras_s2_get_dsp_nc_supported(),
        static_cast<bool>(iodev->nodes[0].nc_providers & CRAS_NC_PROVIDER_DSP));

    ucm_section_free_list(section);
    alsa_iodev_destroy(iodev);

    // Create the IO device for Keyboard Microphone.
    iodev = alsa_iodev_create_with_default_parameters(
        0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
        CRAS_STREAM_INPUT);
    ASSERT_NE(iodev, (void*)NULL);

    // Create the node.
    section = ucm_section_create(KEYBOARD_MIC, "hw:0,2", 0, -1,
                                 CRAS_STREAM_INPUT, NULL, NULL);
    ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
    ASSERT_NE(iodev->nodes, (void*)NULL);

    // ucm_node_noise_cancellation_exists is 0 for Keyboard Microphone.
    // node.nc_providers should not have CRAS_NC_PROVIDER_DSP.
    ASSERT_EQ(iodev->nodes[0].nc_providers & CRAS_NC_PROVIDER_DSP, 0);

    ucm_section_free_list(section);
    alsa_iodev_destroy(iodev);
  }
}

TEST(AlsaOutputNode, InputBypassBlockNoiseCancellation) {
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  struct ucm_section* section;
  int i;

  // i = 0: sys_aec_on_dsp_supported is 0, ucm_aec_exists is 0
  // i = 1: sys_aec_on_dsp_supported is 0, ucm_aec_exists is 1
  // i = 2: sys_aec_on_dsp_supported is 1, ucm_aec_exists is 0
  // i = 3: sys_aec_on_dsp_supported is 1, ucm_aec_exists is 1
  for (i = 0; i < 4; i++) {
    ResetStubData();
    cras_s2_set_dsp_nc_supported(true);
    sys_aec_on_dsp_supported_return_value = i / 2;
    ucm_node_echo_cancellation_exists_ret_value = i % 2;

    // Create the IO device for Internal Microphone.
    iodev = alsa_iodev_create_with_default_parameters(
        0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
        CRAS_STREAM_INPUT);
    ASSERT_NE(iodev, (void*)NULL);

    // Create the node.
    section = ucm_section_create(INTERNAL_MICROPHONE, "hw:0,1", 0, -1,
                                 CRAS_STREAM_INPUT, NULL, NULL);
    ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
    ASSERT_NE(iodev->nodes, (void*)NULL);

    // NC flag in node.audio_effect should be unrelated to AEC on DSP states.
    ASSERT_EQ(iodev->nodes[0].nc_providers & CRAS_NC_PROVIDER_DSP,
              CRAS_NC_PROVIDER_DSP);

    ucm_section_free_list(section);
    alsa_iodev_destroy(iodev);
  }
}

TEST(AlsaOutputNode, InputNoControlsUCM) {
  struct alsa_io* aio;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  struct ucm_section* section;

  ResetStubData();

  // Create the IO device.
  iodev = alsa_iodev_create_with_default_parameters(
      1, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_INPUT);
  ASSERT_NE(iodev, (void*)NULL);
  aio = reinterpret_cast<struct alsa_io*>(iodev);

  // Node without controls or jacks.
  section = ucm_section_create(INTERNAL_MICROPHONE, "hw:0,1", 1, -1,
                               CRAS_STREAM_INPUT, NULL, NULL);
  // Device index doesn't match.
  EXPECT_EQ(-22, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  section->dev_idx = 0;
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  EXPECT_EQ(1, cras_alsa_jack_list_add_jack_for_section_called);
  EXPECT_EQ(1, cras_alsa_mixer_get_control_for_section_called);
  EXPECT_EQ(0, cras_alsa_mixer_get_control_name_called);
  EXPECT_EQ(1, cras_iodev_add_node_called);
  ucm_section_free_list(section);

  // Complete initialization, and make first node active.
  alsa_iodev_ucm_complete_init(iodev);
  EXPECT_EQ(SND_PCM_STREAM_CAPTURE, aio->common.alsa_stream);
  EXPECT_EQ(0, cras_alsa_mixer_get_control_name_called);
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  EXPECT_EQ(0, cras_alsa_jack_enable_ucm_called);
  EXPECT_EQ(1, ucm_set_enabled_called);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaOutputNode, InputFromJackUCM) {
  struct alsa_io* aio;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  static const char* jack_name = "TestCard - Headset Jack";
  struct ucm_section* section;

  ResetStubData();

  // Create the IO device.
  iodev = alsa_iodev_create_with_default_parameters(
      1, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_INPUT);
  ASSERT_NE(iodev, (void*)NULL);
  aio = reinterpret_cast<struct alsa_io*>(iodev);

  // Node without controls or jacks.
  cras_alsa_jack_list_add_jack_for_section_result_jack =
      reinterpret_cast<struct cras_alsa_jack*>(1);
  section = ucm_section_create(MIC, "hw:0,1", 0, -1, CRAS_STREAM_INPUT,
                               jack_name, "hctl");
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  EXPECT_EQ(1, cras_alsa_mixer_get_control_for_section_called);
  EXPECT_EQ(1, cras_iodev_add_node_called);
  EXPECT_EQ(1, cras_alsa_jack_list_add_jack_for_section_called);
  ucm_section_free_list(section);

  // Complete initialization, and make first node active.
  alsa_iodev_ucm_complete_init(iodev);
  EXPECT_EQ(SND_PCM_STREAM_CAPTURE, aio->common.alsa_stream);
  EXPECT_EQ(0, cras_alsa_mixer_get_control_name_called);
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  EXPECT_EQ(1, cras_alsa_jack_enable_ucm_called);
  EXPECT_EQ(0, ucm_set_enabled_called);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaOutputNode, AutoUnplugOutputNode) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct mixer_control* outputs[2];
  const struct cras_alsa_jack* jack = (struct cras_alsa_jack*)4;

  ResetStubData();
  outputs[0] = reinterpret_cast<struct mixer_control*>(5);
  outputs[1] = reinterpret_cast<struct mixer_control*>(6);

  cras_alsa_mixer_list_outputs_outputs = outputs;
  cras_alsa_mixer_list_outputs_outputs_length = ARRAY_SIZE(outputs);

  cras_alsa_mixer_get_control_name_values[outputs[0]] = INTERNAL_SPEAKER;
  cras_alsa_mixer_get_control_name_values[outputs[1]] = HEADPHONE;
  auto_unplug_output_node_ret = 1;

  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);

  // Add headphone jack
  cras_alsa_jack_found_num = 1;
  cras_alsa_jack_found_val[0] = jack;
  cras_alsa_jack_get_name_ret_value = "Headphone Jack";
  is_utf8_string_ret_value = 1;
  cras_alsa_jack_get_mixer_ret = outputs[1];

  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(5, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);
  EXPECT_EQ(2, cras_alsa_mixer_get_control_name_called);

  // Assert that the the internal speaker is plugged and other nodes aren't.
  ASSERT_NE(aio->common.base.nodes, (void*)NULL);
  EXPECT_EQ(aio->common.base.nodes->plugged, 1);
  ASSERT_NE(aio->common.base.nodes->next, (void*)NULL);
  EXPECT_EQ(aio->common.base.nodes->next->plugged, 0);

  // Plug headphone jack
  jack_plug_cb(jack, 1, jack_plug_cb_data);

  // Assert internal speaker is auto unplugged
  EXPECT_EQ(aio->common.base.nodes->plugged, 0);
  EXPECT_EQ(aio->common.base.nodes->next->plugged, 1);

  alsa_iodev_destroy((struct cras_iodev*)aio);
}

TEST(AlsaOutputNode, AutoUnplugInputNode) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct mixer_control* inputs[2];
  const struct cras_alsa_jack* jack = (struct cras_alsa_jack*)4;

  ResetStubData();
  inputs[0] = reinterpret_cast<struct mixer_control*>(5);
  inputs[1] = reinterpret_cast<struct mixer_control*>(6);

  cras_alsa_mixer_list_inputs_outputs = inputs;
  cras_alsa_mixer_list_inputs_outputs_length = ARRAY_SIZE(inputs);

  cras_alsa_mixer_get_control_name_values[inputs[0]] = INTERNAL_MICROPHONE;
  cras_alsa_mixer_get_control_name_values[inputs[1]] = MIC;
  auto_unplug_input_node_ret = 1;

  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_INPUT);

  // Add mic jack
  cras_alsa_jack_found_num = 1;
  cras_alsa_jack_found_val[0] = jack;
  cras_alsa_jack_get_name_ret_value = "Mic Jack";
  is_utf8_string_ret_value = 1;
  cras_alsa_jack_get_mixer_ret = inputs[1];

  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(1, cras_alsa_mixer_list_inputs_called);
  EXPECT_EQ(2, cras_alsa_mixer_get_control_name_called);

  // Assert that the the internal speaker is plugged and other nodes aren't.
  ASSERT_NE(aio->common.base.nodes, (void*)NULL);
  EXPECT_EQ(aio->common.base.nodes->plugged, 1);
  ASSERT_NE(aio->common.base.nodes->next, (void*)NULL);
  EXPECT_EQ(aio->common.base.nodes->next->plugged, 0);

  // Plug headphone jack
  jack_plug_cb(jack, 1, jack_plug_cb_data);

  // Assert internal speaker is auto unplugged
  EXPECT_EQ(aio->common.base.nodes->plugged, 0);
  EXPECT_EQ(aio->common.base.nodes->next->plugged, 1);

  alsa_iodev_destroy((struct cras_iodev*)aio);
}

TEST(AlsaLoopback, InitializePlayback) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  static const char* jack_name = "TestCard - Alsa Loopback";
  struct mixer_control* outputs[1];
  struct ucm_section* section;

  ResetStubData();
  outputs[0] = reinterpret_cast<struct mixer_control*>(3);
  cras_alsa_mixer_list_outputs_outputs = outputs;
  cras_alsa_mixer_list_outputs_outputs_length = ARRAY_SIZE(outputs);
  cras_alsa_mixer_get_control_name_values[outputs[0]] = LOOPBACK_PLAYBACK;

  // Create the IO device.
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);
  ASSERT_NE(iodev, (void*)NULL);
  aio = reinterpret_cast<struct alsa_io*>(iodev);

  // Add node.
  section = ucm_section_create(LOOPBACK_PLAYBACK, "hw:0,1", 0, -1,
                               CRAS_STREAM_OUTPUT, jack_name, NULL);
  ucm_section_set_mixer_name(section, LOOPBACK_PLAYBACK);
  cras_alsa_jack_list_add_jack_for_section_result_jack = NULL;
  cras_alsa_mixer_get_control_for_section_return_value = outputs[0];
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  ucm_section_free_list(section);

  // Complete initialization, and check the loopback playback node is plugged as
  // the active node.
  alsa_iodev_ucm_complete_init(iodev);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->common.alsa_stream);
  ASSERT_NE(aio->common.base.active_node, (void*)NULL);
  EXPECT_STREQ(LOOPBACK_PLAYBACK, aio->common.base.active_node->name);
  EXPECT_EQ(1, aio->common.base.active_node->plugged);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaLoopback, InitializeCapture) {
  struct alsa_io* aio;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  struct cras_iodev* iodev;
  static const char* jack_name = "TestCard - Alsa Loopback";
  struct ucm_section* section;

  ResetStubData();

  // Create the IO device.
  iodev = alsa_iodev_create_with_default_parameters(
      1, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_INPUT);
  ASSERT_NE(iodev, (void*)NULL);
  aio = reinterpret_cast<struct alsa_io*>(iodev);

  // Node without controls or jacks.
  cras_alsa_jack_list_add_jack_for_section_result_jack =
      reinterpret_cast<struct cras_alsa_jack*>(1);
  section = ucm_section_create(LOOPBACK_CAPTURE, "hw:0,1", 0, -1,
                               CRAS_STREAM_INPUT, jack_name, NULL);
  ASSERT_EQ(0, alsa_iodev_ucm_add_nodes_and_jacks(iodev, section));
  ucm_section_free_list(section);

  // Complete initialization, and check the loopback capture node is plugged as
  // the active node.
  alsa_iodev_ucm_complete_init(iodev);
  EXPECT_EQ(SND_PCM_STREAM_CAPTURE, aio->common.alsa_stream);
  ASSERT_NE(aio->common.base.active_node, (void*)NULL);
  EXPECT_STREQ(LOOPBACK_CAPTURE, aio->common.base.active_node->name);
  EXPECT_EQ(1, aio->common.base.active_node->plugged);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaInitNode, SetNodeInitialState) {
  struct cras_ionode node;
  struct cras_iodev dev;

  memset(&dev, 0, sizeof(dev));
  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Unknown");
  dev.direction = CRAS_STREAM_OUTPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(0, node.plugged_time.tv_sec);
  ASSERT_EQ(CRAS_NODE_TYPE_UNKNOWN, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, INTERNAL_SPEAKER);
  dev.direction = CRAS_STREAM_OUTPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(1, node.plugged);
  ASSERT_GT(node.plugged_time.tv_sec, 0);
  ASSERT_EQ(CRAS_NODE_TYPE_INTERNAL_SPEAKER, node.type);
  ASSERT_EQ(NODE_POSITION_INTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, INTERNAL_MICROPHONE);
  dev.direction = CRAS_STREAM_INPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(1, node.plugged);
  ASSERT_GT(node.plugged_time.tv_sec, 0);
  ASSERT_EQ(CRAS_NODE_TYPE_MIC, node.type);
  ASSERT_EQ(NODE_POSITION_INTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, HDMI);
  dev.direction = CRAS_STREAM_OUTPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(0, node.plugged_time.tv_sec);
  ASSERT_EQ(CRAS_NODE_TYPE_HDMI, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "IEC958");
  dev.direction = CRAS_STREAM_OUTPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_HDMI, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "HDMI Jack");
  dev.direction = CRAS_STREAM_OUTPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_HDMI, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Something HDMI Jack");
  dev.direction = CRAS_STREAM_OUTPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_HDMI, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, HEADPHONE);
  dev.direction = CRAS_STREAM_OUTPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_HEADPHONE, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Headphone Jack");
  dev.direction = CRAS_STREAM_OUTPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_HEADPHONE, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, MIC);
  dev.direction = CRAS_STREAM_INPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_MIC, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Front Mic");
  dev.direction = CRAS_STREAM_INPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(1, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_MIC, node.type);
  ASSERT_EQ(NODE_POSITION_FRONT, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Rear Mic");
  dev.direction = CRAS_STREAM_INPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(1, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_MIC, node.type);
  ASSERT_EQ(NODE_POSITION_REAR, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Mic Jack");
  dev.direction = CRAS_STREAM_INPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_MIC, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  dev.direction = CRAS_STREAM_INPUT;
  strcpy(node.name, "DAISY-I2S Mic Jack");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_MIC, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);
  // Node name is changed to "MIC".
  ASSERT_EQ(0, strcmp(node.name, MIC));

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  dev.direction = CRAS_STREAM_OUTPUT;
  strcpy(node.name, "DAISY-I2S Headphone Jack");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_HEADPHONE, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);
  // Node name is changed to "Headphone".
  ASSERT_EQ(0, strcmp(node.name, HEADPHONE));

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Haptic");
  dev.direction = CRAS_STREAM_OUTPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(1, node.plugged);
  ASSERT_GT(node.plugged_time.tv_sec, 0);
  ASSERT_EQ(CRAS_NODE_TYPE_HAPTIC, node.type);
  ASSERT_EQ(NODE_POSITION_INTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Rumbler");
  dev.direction = CRAS_STREAM_OUTPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(1, node.plugged);
  ASSERT_GT(node.plugged_time.tv_sec, 0);
  ASSERT_EQ(CRAS_NODE_TYPE_HAPTIC, node.type);
  ASSERT_EQ(NODE_POSITION_INTERNAL, node.position);
}

TEST(AlsaInitNode, SetNodeInitialStateDropInvalidUTF8NodeName) {
  struct cras_ionode node;
  struct cras_iodev dev;

  memset(&dev, 0, sizeof(dev));
  memset(&node, 0, sizeof(node));
  node.dev = &dev;

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Something HDMI Jack");
  // 0xfe can not appear in a valid UTF-8 string.
  node.name[0] = 0xfe;
  is_utf8_string_ret_value = 0;
  dev.direction = CRAS_STREAM_OUTPUT;
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(CRAS_NODE_TYPE_HDMI, node.type);
  ASSERT_STREQ(HDMI, node.name);
}

TEST(AlsaIoInit, HDMIJackUpdateInvalidUTF8MonitorName) {
  struct alsa_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_use_case_mgr* const fake_ucm = (struct cras_use_case_mgr*)3;
  const struct cras_alsa_jack* hdmi_jack = (struct cras_alsa_jack*)4;

  ResetStubData();
  aio = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, fake_ucm,
      CRAS_STREAM_OUTPUT);

  // Prepare the stub data such that the jack will be identified as an
  // HDMI jack, and thus the callback creates an HDMI node.
  cras_alsa_jack_found_num = 1;
  cras_alsa_jack_found_val[0] = hdmi_jack;
  cras_alsa_jack_get_name_ret_value = "HDMI Jack";
  // Set the jack name updated from monitor to be an invalid UTF8 string.
  cras_alsa_jack_update_monitor_fake_name = "\xfeomething";
  is_utf8_string_ret_value = 0;

  ASSERT_EQ(0, alsa_iodev_legacy_complete_init((struct cras_iodev*)aio));

  EXPECT_EQ(2, cras_alsa_jack_get_name_called);

  ASSERT_EQ(CRAS_NODE_TYPE_HDMI, aio->common.base.nodes->type);
  // The node name should be "HDMI".
  ASSERT_STREQ(HDMI, aio->common.base.nodes->name);

  alsa_iodev_destroy((struct cras_iodev*)aio);
}

//  Test thread add/rm stream, open_alsa, and iodev config.
class AlsaVolumeMuteSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    output_control_ = reinterpret_cast<struct mixer_control*>(10);
    cras_alsa_mixer_list_outputs_outputs = &output_control_;
    cras_alsa_mixer_list_outputs_outputs_length = 1;
    cras_alsa_mixer_get_control_name_values[output_control_] = INTERNAL_SPEAKER;
    cras_alsa_mixer_list_outputs_outputs_length = 1;
    aio_output_ = (struct alsa_io*)alsa_iodev_create_with_default_parameters(
        0, NULL, ALSA_CARD_TYPE_INTERNAL, 1, fake_mixer, fake_config, NULL,
        CRAS_STREAM_OUTPUT);
    hp_jack = (struct cras_alsa_jack*)4;

    // Add headphone jack with its own volume curve.
    cras_alsa_jack_get_mixer_ret = NULL;
    cras_alsa_jack_get_name_ret_value = HEADPHONE;
    cras_card_config_get_volume_curve_vals[HEADPHONE] = &hp_curve;
    cras_alsa_jack_found_num = 1;
    cras_alsa_jack_found_val[0] = hp_jack;

    alsa_iodev_legacy_complete_init((struct cras_iodev*)aio_output_);

    EXPECT_EQ(1, cras_alsa_jack_update_node_type_called);
    EXPECT_EQ(3, cras_card_config_get_volume_curve_for_control_called);

    struct cras_ionode* node;
    int count = 0;
    DL_FOREACH (aio_output_->common.base.nodes, node) {
      printf("node %d \n", count);
    }
    aio_output_->common.base.direction = CRAS_STREAM_OUTPUT;
    fmt_.frame_rate = 44100;
    fmt_.num_channels = 2;
    fmt_.format = SND_PCM_FORMAT_S16_LE;
    aio_output_->common.base.format = &fmt_;
    cras_alsa_get_avail_frames_ret = -1;
  }

  virtual void TearDown() {
    alsa_iodev_destroy((struct cras_iodev*)aio_output_);
    cras_alsa_get_avail_frames_ret = 0;
  }

  struct cras_volume_curve hp_curve = {
      .get_dBFS = fake_get_dBFS,
  };
  struct cras_alsa_jack* hp_jack;
  struct mixer_control* output_control_;
  struct alsa_io* aio_output_;
  struct cras_audio_format fmt_;
};

TEST_F(AlsaVolumeMuteSuite, GetDefaultVolumeCurve) {
  int rc;
  struct cras_audio_format* fmt;

  fmt = (struct cras_audio_format*)malloc(sizeof(*fmt));
  memcpy(fmt, &fmt_, sizeof(fmt_));
  aio_output_->common.base.format = fmt;
  aio_output_->common.handle = (snd_pcm_t*)0x24;

  rc = aio_output_->common.base.configure_dev(&aio_output_->common.base);
  ASSERT_EQ(0, rc);
  EXPECT_EQ(&default_curve, fake_get_dBFS_volume_curve_val);

  aio_output_->common.base.set_volume(&aio_output_->common.base);
  EXPECT_EQ(&default_curve, fake_get_dBFS_volume_curve_val);
  aio_output_->common.base.close_dev(&aio_output_->common.base);
  free(fmt);
}

TEST_F(AlsaVolumeMuteSuite, GetVolumeCurveFromNode) {
  int rc;
  struct cras_audio_format* fmt;
  struct cras_ionode* node;

  // Headphone jack plugged
  jack_plug_cb(hp_jack, 1, jack_plug_cb_data);

  // These settings should be placed after plugging jacks to make it safer.
  // If is HDMI jack, plug event will trigger update_max_supported_channels()
  // and do open_dev() and close_dev() once. close_dev() will perform alsa_io
  // cleanup.
  // Headphone jack won't trigger, but we still place here due to coherence.
  fmt = (struct cras_audio_format*)malloc(sizeof(*fmt));
  memcpy(fmt, &fmt_, sizeof(fmt_));
  aio_output_->common.base.format = fmt;
  aio_output_->common.handle = (snd_pcm_t*)0x24;

  // Switch to node 'Headphone'.
  node = aio_output_->common.base.nodes->next;
  aio_output_->common.base.active_node = node;

  rc = aio_output_->common.base.configure_dev(&aio_output_->common.base);
  ASSERT_EQ(0, rc);
  EXPECT_EQ(&hp_curve, fake_get_dBFS_volume_curve_val);

  aio_output_->common.base.set_volume(&aio_output_->common.base);
  EXPECT_EQ(&hp_curve, fake_get_dBFS_volume_curve_val);
  aio_output_->common.base.close_dev(&aio_output_->common.base);
  free(fmt);
}

TEST_F(AlsaVolumeMuteSuite, SetVolume) {
  int rc;
  struct cras_audio_format* fmt;
  const size_t fake_system_volume = 55;
  const size_t fake_system_volume_dB = (fake_system_volume - 100) * 100;

  ResetStubData();

  fmt = (struct cras_audio_format*)malloc(sizeof(*fmt));
  memcpy(fmt, &fmt_, sizeof(fmt_));
  aio_output_->common.base.format = fmt;
  aio_output_->common.handle = (snd_pcm_t*)0x24;

  sys_get_volume_return_value = fake_system_volume;
  rc = aio_output_->common.base.configure_dev(&aio_output_->common.base);
  ASSERT_EQ(0, rc);
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  EXPECT_EQ(fake_system_volume_dB, alsa_mixer_set_dBFS_value);

  alsa_mixer_set_dBFS_called = 0;
  alsa_mixer_set_dBFS_value = 0;
  sys_get_volume_return_value = 50;
  sys_get_volume_called = 0;
  aio_output_->common.base.set_volume(&aio_output_->common.base);
  EXPECT_EQ(1, sys_get_volume_called);
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  EXPECT_EQ(-5000, alsa_mixer_set_dBFS_value);
  EXPECT_EQ(output_control_, alsa_mixer_set_dBFS_output);

  alsa_mixer_set_dBFS_called = 0;
  alsa_mixer_set_dBFS_value = 0;
  sys_get_volume_return_value = 0;
  sys_get_volume_called = 0;
  aio_output_->common.base.set_volume(&aio_output_->common.base);
  EXPECT_EQ(1, sys_get_volume_called);
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  EXPECT_EQ(-10000, alsa_mixer_set_dBFS_value);

  sys_get_volume_return_value = 80;
  aio_output_->common.base.active_node->volume = 90;
  aio_output_->common.base.set_volume(&aio_output_->common.base);
  EXPECT_EQ(-3000, alsa_mixer_set_dBFS_value);

  // close the dev.
  rc = aio_output_->common.base.close_dev(&aio_output_->common.base);
  EXPECT_EQ(0, rc);
  EXPECT_EQ((void*)NULL, aio_output_->common.handle);

  free(fmt);
}

TEST_F(AlsaVolumeMuteSuite, SetMute) {
  int muted;

  aio_output_->common.handle = (snd_pcm_t*)0x24;

  // Test mute.
  ResetStubData();
  muted = 1;

  sys_get_mute_return_value = muted;

  aio_output_->common.base.set_mute(&aio_output_->common.base);

  EXPECT_EQ(1, sys_get_mute_called);
  EXPECT_EQ(1, alsa_mixer_set_mute_called);
  EXPECT_EQ(muted, alsa_mixer_set_mute_value);
  EXPECT_EQ(output_control_, alsa_mixer_set_mute_output);

  // Test unmute.
  ResetStubData();
  muted = 0;

  sys_get_mute_return_value = muted;

  aio_output_->common.base.set_mute(&aio_output_->common.base);

  EXPECT_EQ(1, sys_get_mute_called);
  EXPECT_EQ(1, alsa_mixer_set_mute_called);
  EXPECT_EQ(muted, alsa_mixer_set_mute_value);
  EXPECT_EQ(output_control_, alsa_mixer_set_mute_output);
}

//  Test free run.
class AlsaFreeRunTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    memset(&aio, 0, sizeof(aio));
    fmt_.format = SND_PCM_FORMAT_S16_LE;
    fmt_.frame_rate = 48000;
    fmt_.num_channels = 2;
    aio.common.base.frames_queued = frames_queued;
    aio.common.base.output_underrun = alsa_output_underrun;
    aio.common.base.direction = CRAS_STREAM_OUTPUT;
    aio.common.base.format = &fmt_;
    aio.common.base.buffer_size = BUFFER_SIZE;
    aio.common.base.min_cb_level = 240;
    aio.common.base.min_buffer_level = 0;
    aio.common.filled_zeros_for_draining = 0;
    cras_alsa_mmap_begin_buffer = (uint8_t*)calloc(
        BUFFER_SIZE * 2 * 2, sizeof(*cras_alsa_mmap_begin_buffer));
    memset(cras_alsa_mmap_begin_buffer, 0xff,
           sizeof(*cras_alsa_mmap_begin_buffer));
  }

  virtual void TearDown() { free(cras_alsa_mmap_begin_buffer); }

  struct alsa_io aio;
  struct cras_audio_format fmt_;
};

TEST_F(AlsaFreeRunTestSuite, FillWholeBufferWithZeros) {
  int rc;
  int16_t* zeros;

  rc = fill_whole_buffer_with_zeros(&aio.common.base);

  EXPECT_EQ(aio.common.base.buffer_size, rc);
  zeros = (int16_t*)calloc(BUFFER_SIZE * 2, sizeof(*zeros));
  EXPECT_EQ(0, memcmp(zeros, cras_alsa_mmap_begin_buffer, BUFFER_SIZE * 2 * 2));

  free(zeros);
}

TEST_F(AlsaFreeRunTestSuite, EnterFreeRunAlreadyFreeRunning) {
  int rc;

  // Device is in free run state, no need to fill zeros or fill whole buffer.
  aio.common.free_running = 1;

  rc = no_stream(&aio.common.base, 1);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_alsa_mmap_get_whole_buffer_called);
  EXPECT_EQ(0, cras_iodev_fill_odev_zeros_called);
  EXPECT_EQ(0, cras_iodev_fill_odev_zeros_frames);
}

TEST_F(AlsaFreeRunTestSuite, EnterFreeRunNotDrainedYetNeedToFillZeros) {
  int rc, real_hw_level;
  struct timespec hw_tstamp;
  int fill_zeros_duration = 50;
  // Device is not in free run state. There are still valid samples to play.
  // In cras_alsa_io.c, we defined there are 50ms zeros to be filled.
  real_hw_level = 200;
  cras_alsa_get_avail_frames_avail = BUFFER_SIZE - real_hw_level;

  rc = aio.common.base.frames_queued(&aio.common.base, &hw_tstamp);
  EXPECT_EQ(200, rc);

  rc = no_stream(&aio.common.base, 1);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_alsa_mmap_get_whole_buffer_called);
  EXPECT_EQ(1, cras_iodev_fill_odev_zeros_called);
  EXPECT_EQ(fmt_.frame_rate / 1000 * fill_zeros_duration,
            cras_iodev_fill_odev_zeros_frames);
  EXPECT_EQ(fmt_.frame_rate / 1000 * fill_zeros_duration,
            aio.common.filled_zeros_for_draining);
  EXPECT_EQ(0, aio.common.free_running);
}

TEST_F(AlsaFreeRunTestSuite, EnterFreeRunNotDrainedYetFillZerosExceedBuffer) {
  int rc, real_hw_level;

  // Device is not in free run state. There are still valid samples to play.
  // If frames avail is smaller than 50ms(48 * 50 = 2400 zeros), only fill
  // zeros until buffer size.
  real_hw_level = 7000;
  cras_alsa_get_avail_frames_avail = BUFFER_SIZE - real_hw_level;

  rc = no_stream(&aio.common.base, 1);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_alsa_mmap_get_whole_buffer_called);
  EXPECT_EQ(1, cras_iodev_fill_odev_zeros_called);
  EXPECT_EQ(cras_alsa_get_avail_frames_avail,
            cras_iodev_fill_odev_zeros_frames);
  EXPECT_EQ(cras_alsa_get_avail_frames_avail,
            aio.common.filled_zeros_for_draining);
  EXPECT_EQ(0, aio.common.free_running);
}

TEST_F(AlsaFreeRunTestSuite, EnterFreeRunDrained) {
  int rc, real_hw_level;

  // Device is not in free run state. There are still valid samples to play.
  // The number of valid samples is less than filled zeros.
  // Should enter free run state and fill whole buffer with zeros.
  real_hw_level = 40;
  cras_alsa_get_avail_frames_avail = BUFFER_SIZE - real_hw_level;
  aio.common.filled_zeros_for_draining = 100;

  rc = no_stream(&aio.common.base, 1);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_alsa_mmap_get_whole_buffer_called);
  EXPECT_EQ(0, cras_iodev_fill_odev_zeros_called);
  EXPECT_EQ(1, aio.common.free_running);
}

TEST_F(AlsaFreeRunTestSuite, EnterFreeRunNoSamples) {
  int rc, real_hw_level;

  // Device is not in free run state. There is no sample to play.
  // Should enter free run state and fill whole buffer with zeros.
  real_hw_level = 0;
  cras_alsa_get_avail_frames_avail = BUFFER_SIZE - real_hw_level;

  rc = no_stream(&aio.common.base, 1);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_alsa_mmap_get_whole_buffer_called);
  EXPECT_EQ(0, cras_iodev_fill_odev_zeros_called);
  EXPECT_EQ(1, aio.common.free_running);
}

TEST_F(AlsaFreeRunTestSuite, IsFreeRunning) {
  aio.common.free_running = 1;
  EXPECT_EQ(1, is_free_running(&aio.common.base));

  aio.common.free_running = 0;
  EXPECT_EQ(0, is_free_running(&aio.common.base));
}

TEST_F(AlsaFreeRunTestSuite, LeaveFreeRunNotInFreeRunMoreRemain) {
  int rc, real_hw_level;

  // Compare min_buffer_level + min_cb_level with valid samples left.
  // 240 + 512 < 900 - 100, so we will get 900 - 100 in appl_ptr_ahead.

  aio.common.free_running = 0;
  aio.common.filled_zeros_for_draining = 100;
  aio.common.base.min_buffer_level = 512;
  real_hw_level = 900;
  cras_alsa_get_avail_frames_avail = BUFFER_SIZE - real_hw_level;

  rc = no_stream(&aio.common.base, 0);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_alsa_resume_appl_ptr_called);
  EXPECT_EQ(800, cras_alsa_resume_appl_ptr_ahead);
  EXPECT_EQ(0, cras_iodev_fill_odev_zeros_frames);
  EXPECT_EQ(0, aio.common.free_running);
  EXPECT_EQ(0, aio.common.filled_zeros_for_draining);
  EXPECT_EQ(1, cras_iodev_reset_rate_estimator_called);
}

TEST_F(AlsaFreeRunTestSuite, LeaveFreeRunNotInFreeRunLessRemain) {
  int rc, real_hw_level;

  // Compare min_buffer_level + min_cb_level with valid samples left.
  // 240 + 256 > 400 - 500, so we will get 240 + 256 in appl_ptr_ahead.
  // And it will fill 240 + 256 - 400 = 96 zeros frames into device.

  aio.common.free_running = 0;
  aio.common.filled_zeros_for_draining = 500;
  aio.common.base.min_buffer_level = 256;
  real_hw_level = 400;
  cras_alsa_get_avail_frames_avail = BUFFER_SIZE - real_hw_level;

  rc = no_stream(&aio.common.base, 0);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_alsa_resume_appl_ptr_called);
  EXPECT_EQ(aio.common.base.min_buffer_level + aio.common.base.min_cb_level,
            cras_alsa_resume_appl_ptr_ahead);
  EXPECT_EQ(96, cras_iodev_fill_odev_zeros_frames);
  EXPECT_EQ(0, aio.common.free_running);
  EXPECT_EQ(0, aio.common.filled_zeros_for_draining);
  EXPECT_EQ(1, cras_iodev_reset_rate_estimator_called);
}

TEST_F(AlsaFreeRunTestSuite, LeaveFreeRunInFreeRun) {
  int rc;

  aio.common.free_running = 1;
  aio.common.filled_zeros_for_draining = 100;
  aio.common.base.min_buffer_level = 512;

  rc = no_stream(&aio.common.base, 0);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_alsa_resume_appl_ptr_called);
  EXPECT_EQ(aio.common.base.min_buffer_level + aio.common.base.min_cb_level,
            cras_alsa_resume_appl_ptr_ahead);
  EXPECT_EQ(0, aio.common.free_running);
  EXPECT_EQ(0, aio.common.filled_zeros_for_draining);
  EXPECT_EQ(1, cras_iodev_reset_rate_estimator_called);
}

// Reuse AlsaFreeRunTestSuite for output underrun handling because they are
// similar.
TEST_F(AlsaFreeRunTestSuite, OutputUnderrun) {
  int rc;
  int16_t* zeros;
  snd_pcm_uframes_t offset;

  // Ask alsa_io to handle output underrun.
  rc = alsa_output_underrun(&aio.common.base);
  EXPECT_EQ(aio.common.base.buffer_size, rc);
  EXPECT_EQ(1, cras_iodev_update_underrun_duration_called);

  // mmap buffer should be filled with zeros.
  zeros = (int16_t*)calloc(BUFFER_SIZE * 2, sizeof(*zeros));
  EXPECT_EQ(0, memcmp(zeros, cras_alsa_mmap_begin_buffer, BUFFER_SIZE * 2 * 2));

  // appl_ptr should be moved to min_buffer_level + 1.5 * min_cb_level ahead of
  // hw_ptr.
  offset = aio.common.base.min_buffer_level + aio.common.base.min_cb_level +
           aio.common.base.min_cb_level / 2;
  EXPECT_EQ(1, cras_alsa_resume_appl_ptr_called);
  EXPECT_EQ(offset, cras_alsa_resume_appl_ptr_ahead);

  free(zeros);
}

TEST(AlsaHotwordNode, HotwordTriggeredSendMessage) {
  struct cras_iodev* iodev;
  struct cras_audio_format format;
  struct alsa_input_node alsa_node;
  struct cras_ionode* node = &alsa_node.common.base;
  int rc;

  ResetStubData();
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_INPUT);
  format.frame_rate = 16000;
  format.num_channels = 1;
  cras_iodev_set_format(iodev, &format);

  memset(&alsa_node, 0, sizeof(alsa_node));
  node->dev = iodev;
  strcpy(node->name, HOTWORD_DEV);
  set_node_initial_state(node, ALSA_CARD_TYPE_INTERNAL);
  EXPECT_EQ(CRAS_NODE_TYPE_HOTWORD, node->type);

  iodev->active_node = node;
  iodev->open_dev(iodev);
  rc = iodev->configure_dev(iodev);
  free(fake_format);
  ASSERT_EQ(0, rc);

  ASSERT_NE(reinterpret_cast<thread_callback>(NULL), audio_thread_cb);
  audio_thread_cb(audio_thread_cb_data, POLLIN);
  EXPECT_EQ(1, hotword_send_triggered_msg_called);
  iodev->close_dev(iodev);
  alsa_iodev_destroy(iodev);
}

TEST(AlsaGetValidFrames, GetValidFramesNormalState) {
  struct cras_iodev* iodev;
  struct alsa_io* aio;
  struct timespec tstamp;
  int rc;

  ResetStubData();
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  aio = (struct alsa_io*)iodev;

  aio->common.free_running = 0;
  aio->common.filled_zeros_for_draining = 200;
  cras_alsa_get_avail_frames_avail = iodev->buffer_size - 500;
  cras_alsa_get_avail_frames_ret = 0;
  clock_gettime_retspec.tv_sec = 123;
  clock_gettime_retspec.tv_nsec = 321;
  rc = iodev->get_valid_frames(iodev, &tstamp);
  EXPECT_EQ(rc, 300);
  EXPECT_EQ(tstamp.tv_sec, clock_gettime_retspec.tv_sec);
  EXPECT_EQ(tstamp.tv_nsec, clock_gettime_retspec.tv_nsec);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaGetValidFrames, GetValidFramesFreeRunning) {
  struct cras_iodev* iodev;
  struct alsa_io* aio;
  struct timespec tstamp;
  int rc;

  ResetStubData();
  iodev = alsa_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_INTERNAL, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  aio = (struct alsa_io*)iodev;

  aio->common.free_running = 1;
  clock_gettime_retspec.tv_sec = 123;
  clock_gettime_retspec.tv_nsec = 321;
  rc = iodev->get_valid_frames(iodev, &tstamp);
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(tstamp.tv_sec, clock_gettime_retspec.tv_sec);
  EXPECT_EQ(tstamp.tv_nsec, clock_gettime_retspec.tv_nsec);

  alsa_iodev_destroy(iodev);
}

}  //  namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  openlog(NULL, LOG_PERROR, LOG_USER);
  return RUN_ALL_TESTS();
}

//  Stubs

extern "C" {

//  From iodev.
int cras_iodev_list_add(struct cras_iodev* output) {
  return 0;
}

int cras_iodev_list_rm(struct cras_iodev* dev) {
  return 0;
}

char* cras_iodev_list_get_hotword_models(cras_node_id_t node_id) {
  return NULL;
}

int cras_iodev_list_set_hotword_model(cras_node_id_t node_id,
                                      const char* model_name) {
  return 0;
}

bool cras_iodev_is_channel_count_supported(struct cras_iodev* dev,
                                           int channel) {
  return true;
}

int cras_iodev_list_suspend_hotword_streams() {
  return 0;
}

int cras_iodev_list_resume_hotword_stream() {
  return 0;
}

struct audio_thread* cras_iodev_list_get_audio_thread() {
  return NULL;
}

//  From alsa helper.
int cras_alsa_set_channel_map(snd_pcm_t* handle,
                              struct cras_audio_format* fmt) {
  return 0;
}
int cras_alsa_get_channel_map(snd_pcm_t* handle,
                              struct cras_audio_format* fmt) {
  return 0;
}
int cras_alsa_pcm_open(snd_pcm_t** handle,
                       const char* dev,
                       snd_pcm_stream_t stream) {
  *handle = (snd_pcm_t*)0x24;
  cras_alsa_open_called++;
  return 0;
}
int cras_alsa_pcm_close(snd_pcm_t* handle) {
  return 0;
}
int cras_alsa_pcm_start(snd_pcm_t* handle) {
  cras_alsa_start_called++;
  return 0;
}
int cras_alsa_pcm_drain(snd_pcm_t* handle) {
  return 0;
}
int cras_alsa_fill_properties(snd_pcm_t* handle,
                              size_t** rates,
                              size_t** channel_counts,
                              snd_pcm_format_t** formats) {
  *rates = (size_t*)malloc(sizeof(**rates) * 3);
  (*rates)[0] = 44100;
  (*rates)[1] = 48000;
  (*rates)[2] = 0;

  if (cras_alsa_support_8_channels) {  // Support up to 8 channels.
    *channel_counts = (size_t*)malloc(sizeof(**channel_counts) * 6);
    (*channel_counts)[0] = 6;
    (*channel_counts)[1] = 4;
    (*channel_counts)[2] = 2;
    (*channel_counts)[3] = 1;
    (*channel_counts)[4] = 8;
    (*channel_counts)[5] = 0;
  } else {  // Support 2 channels only.
    *channel_counts = (size_t*)malloc(sizeof(**channel_counts) * 2);
    (*channel_counts)[0] = 2;
    (*channel_counts)[1] = 0;
  }

  *formats = (snd_pcm_format_t*)malloc(sizeof(**formats) * 2);
  (*formats)[0] = SND_PCM_FORMAT_S16_LE;
  (*formats)[1] = (snd_pcm_format_t)0;

  cras_alsa_fill_properties_called++;
  return 0;
}
int cras_alsa_set_hwparams(snd_pcm_t* handle,
                           struct cras_audio_format* format,
                           snd_pcm_uframes_t* buffer_size,
                           int period_wakeup,
                           unsigned int dma_period_time) {
  if (testing_channel_retry && format->num_channels != 2) {
    return -1;
  }
  return 0;
}
int cras_alsa_set_swparams(snd_pcm_t* handle) {
  return 0;
}
int cras_alsa_get_avail_frames(snd_pcm_t* handle,
                               snd_pcm_uframes_t buf_size,
                               snd_pcm_uframes_t severe_underrun_frames,
                               const char* dev_name,
                               snd_pcm_uframes_t* used,
                               struct timespec* tstamp) {
  *used = cras_alsa_get_avail_frames_avail;
  clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
  return cras_alsa_get_avail_frames_ret;
}
int cras_alsa_get_delay_frames(snd_pcm_t* handle,
                               snd_pcm_uframes_t buf_size,
                               snd_pcm_sframes_t* delay) {
  *delay = 0;
  return 0;
}
int cras_alsa_mmap_begin(snd_pcm_t* handle,
                         unsigned int format_bytes,
                         uint8_t** dst,
                         snd_pcm_uframes_t* offset,
                         snd_pcm_uframes_t* frames) {
  *dst = cras_alsa_mmap_begin_buffer;
  *frames = cras_alsa_mmap_begin_frames;
  return 0;
}
int cras_alsa_mmap_commit(snd_pcm_t* handle,
                          snd_pcm_uframes_t offset,
                          snd_pcm_uframes_t frames) {
  return 0;
}
int cras_alsa_attempt_resume(snd_pcm_t* handle) {
  cras_alsa_attempt_resume_called++;
  return 0;
}
int cras_alsa_get_timestamp(snd_pcm_t* handle,
                            const char* dev_name,
                            struct timespec* tstamp) {
  return 0;
}

//  ALSA stubs.
int snd_pcm_format_physical_width(snd_pcm_format_t format) {
  return 16;
}

snd_pcm_state_t snd_pcm_state(snd_pcm_t* handle) {
  return snd_pcm_state_ret;
}

const char* snd_strerror(int errnum) {
  return "Alsa Error in UT";
}

struct mixer_control* cras_alsa_mixer_get_control_for_section(
    struct cras_alsa_mixer* cras_mixer,
    const struct ucm_section* section) {
  cras_alsa_mixer_get_control_for_section_called++;
  return cras_alsa_mixer_get_control_for_section_return_value;
}

const char* cras_alsa_mixer_get_control_name(
    const struct mixer_control* control) {
  ControlNameMap::iterator it;
  cras_alsa_mixer_get_control_name_called++;
  it = cras_alsa_mixer_get_control_name_values.find(control);
  if (it == cras_alsa_mixer_get_control_name_values.end()) {
    return "";
  }
  return it->second.c_str();
}

//  From system_state.
size_t cras_system_get_volume() {
  sys_get_volume_called++;
  return sys_get_volume_return_value;
}

int cras_system_get_max_internal_speaker_channels() {
  sys_get_max_internal_speaker_channels_called++;
  return sys_get_max_internal_speaker_channels_return_value;
}

//  From system_state.
int cras_system_get_max_headphone_channels() {
  sys_get_max_headphone_channels_called++;
  return sys_get_max_headphone_channels_return_value;
}

int cras_system_get_mute() {
  sys_get_mute_called++;
  return sys_get_mute_return_value;
}

void cras_system_set_volume_limits(long min, long max) {
  sys_set_volume_limits_called++;
}

bool cras_system_get_style_transfer_supported() {
  return false;
}

enum CRAS_SCREEN_ROTATION cras_system_get_display_rotation() {
  return ROTATE_0;
}

int cras_system_aec_on_dsp_supported() {
  return sys_aec_on_dsp_supported_return_value;
}

void cras_system_set_bt_hfp_offload_supported(bool supported) {}

int cras_system_get_using_default_volume_curve_for_usb_audio_device() {
  return sys_using_default_volume_curve_for_usb_audio_device_value;
}

bool cras_system_get_spatial_audio_enabled() {
  return false;
}

//  From cras_alsa_mixer.
void cras_alsa_mixer_set_dBFS(struct cras_alsa_mixer* m,
                              long dB_level,
                              struct mixer_control* output) {
  alsa_mixer_set_dBFS_called++;
  alsa_mixer_set_dBFS_value = dB_level;
  alsa_mixer_set_dBFS_output = output;
}

void cras_alsa_mixer_set_mute(struct cras_alsa_mixer* cras_mixer,
                              int muted,
                              struct mixer_control* mixer_output) {
  alsa_mixer_set_mute_called++;
  alsa_mixer_set_mute_value = muted;
  alsa_mixer_set_mute_output = mixer_output;
}

void cras_alsa_mixer_get_playback_dBFS_range(struct cras_alsa_mixer* cras_mixer,
                                             struct mixer_control* mixer_output,
                                             long* max_volume_dB,
                                             long* min_volume_dB) {
  cras_alsa_mixer_get_playback_dBFS_range_called++;
  *max_volume_dB = cras_alsa_mixer_get_playback_dBFS_range_max;
  *min_volume_dB = cras_alsa_mixer_get_playback_dBFS_range_min;
  return;
}

int cras_alsa_mixer_get_playback_step(struct mixer_control* mixer_output) {
  cras_alsa_mixer_get_playback_step_called++;
  auto it = cras_alsa_mixer_get_playback_step_values.find(mixer_output);
  if (it == cras_alsa_mixer_get_playback_step_values.end()) {
    return 25;
  }
  return it->second;
}

void cras_alsa_mixer_set_capture_dBFS(struct cras_alsa_mixer* m,
                                      long dB_level,
                                      struct mixer_control* mixer_input) {
  alsa_mixer_set_capture_dBFS_called++;
  alsa_mixer_set_capture_dBFS_value = dB_level;
  alsa_mixer_set_capture_dBFS_input = mixer_input;
}

void cras_alsa_mixer_list_outputs(struct cras_alsa_mixer* cras_mixer,
                                  cras_alsa_mixer_control_callback cb,
                                  void* callback_arg) {
  cras_alsa_mixer_list_outputs_called++;
  for (size_t i = 0; i < cras_alsa_mixer_list_outputs_outputs_length; i++) {
    cb(cras_alsa_mixer_list_outputs_outputs[i], callback_arg);
  }
}

void cras_alsa_mixer_list_inputs(struct cras_alsa_mixer* cras_mixer,
                                 cras_alsa_mixer_control_callback cb,
                                 void* callback_arg) {
  cras_alsa_mixer_list_inputs_called++;
  for (size_t i = 0; i < cras_alsa_mixer_list_inputs_outputs_length; i++) {
    cb(cras_alsa_mixer_list_inputs_outputs[i], callback_arg);
  }
}

int cras_alsa_mixer_set_output_active_state(struct mixer_control* output,
                                            int active) {
  cras_alsa_mixer_set_output_active_state_called++;
  cras_alsa_mixer_set_output_active_state_outputs.push_back(output);
  cras_alsa_mixer_set_output_active_state_values.push_back(active);
  return 0;
}

void cras_volume_curve_destroy(struct cras_volume_curve* curve) {}

long cras_alsa_mixer_get_minimum_capture_gain(
    struct cras_alsa_mixer* cmix,
    struct mixer_control* mixer_input) {
  cras_alsa_mixer_get_minimum_capture_gain_called++;
  cras_alsa_mixer_get_minimum_capture_gain_mixer_input = mixer_input;
  return cras_alsa_mixer_get_minimum_capture_gain_ret_value;
}

long cras_alsa_mixer_get_maximum_capture_gain(
    struct cras_alsa_mixer* cmix,
    struct mixer_control* mixer_input) {
  cras_alsa_mixer_get_maximum_capture_gain_called++;
  cras_alsa_mixer_get_maximum_capture_gain_mixer_input = mixer_input;
  return cras_alsa_mixer_get_maximum_capture_gain_ret_value;
}

int cras_alsa_mixer_has_main_volume(const struct cras_alsa_mixer* cras_mixer) {
  return 1;
}

int cras_alsa_mixer_has_volume(const struct mixer_control* mixer_control) {
  return 1;
}

// From cras_alsa_jack
struct cras_alsa_jack_list* cras_alsa_jack_list_create(
    unsigned int card_index,
    const char* card_name,
    unsigned int device_index,
    int check_gpio_jack,
    struct cras_alsa_mixer* mixer,
    struct cras_use_case_mgr* ucm,
    snd_hctl_t* hctl,
    enum CRAS_STREAM_DIRECTION direction,
    jack_state_change_callback* cb,
    void* cb_data) {
  cras_alsa_jack_list_create_called++;
  jack_plug_cb = cb;
  jack_plug_cb_data = cb_data;
  return (struct cras_alsa_jack_list*)0xfee;
}

int cras_alsa_jack_list_find_jacks_by_name_matching(
    struct cras_alsa_jack_list* jack_list,
    jack_found_callback cb,
    void* cb_data) {
  size_t i;
  cras_alsa_jack_list_find_jacks_by_name_matching_called++;
  for (i = 0; i < cras_alsa_jack_found_num; i++) {
    cb(cras_alsa_jack_found_val[i], cb_data);
  }
  return 0;
}

int cras_alsa_jack_list_add_jack_for_section(
    struct cras_alsa_jack_list* jack_list,
    struct ucm_section* ucm_section,
    struct cras_alsa_jack** result_jack) {
  cras_alsa_jack_list_add_jack_for_section_called++;
  if (result_jack) {
    *result_jack = cras_alsa_jack_list_add_jack_for_section_result_jack;
  }
  return 0;
}

void cras_alsa_jack_list_destroy(struct cras_alsa_jack_list* jack_list) {
  cras_alsa_jack_list_destroy_called++;
}

int cras_alsa_jack_list_has_hctl_jacks(struct cras_alsa_jack_list* jack_list) {
  return cras_alsa_jack_list_has_hctl_jacks_return_val;
}

void cras_alsa_jack_list_report(const struct cras_alsa_jack_list* jack_list) {}

void cras_alsa_jack_enable_ucm(const struct cras_alsa_jack* jack, int enable) {
  cras_alsa_jack_enable_ucm_called++;
}

const char* cras_alsa_jack_get_name(const struct cras_alsa_jack* jack) {
  cras_alsa_jack_get_name_called++;
  return cras_alsa_jack_get_name_ret_value;
}

const char* ucm_get_dsp_name_for_dev(struct cras_use_case_mgr* mgr,
                                     const char* dev) {
  DspNameMap::iterator it;
  ucm_get_dsp_name_for_dev_called++;
  if (!dev) {
    return NULL;
  }
  it = ucm_get_dsp_name_for_dev_values.find(dev);
  if (it == ucm_get_dsp_name_for_dev_values.end()) {
    return NULL;
  }
  return strdup(it->second.c_str());
}

struct mixer_control* cras_alsa_jack_get_mixer(
    const struct cras_alsa_jack* jack) {
  return cras_alsa_jack_get_mixer_ret;
}

int ucm_set_enabled(struct cras_use_case_mgr* mgr,
                    const char* dev,
                    int enabled) {
  ucm_set_enabled_called++;
  return 0;
}

char* ucm_get_flag(struct cras_use_case_mgr* mgr, const char* flag_name) {
  if ((!strcmp(flag_name, "AutoUnplugInputNode") &&
       auto_unplug_input_node_ret) ||
      (!strcmp(flag_name, "AutoUnplugOutputNode") &&
       auto_unplug_output_node_ret)) {
    char* ret = (char*)malloc(8);
    snprintf(ret, 8, "%s", "1");
    return ret;
  }

  return NULL;
}

int ucm_swap_mode_exists(struct cras_use_case_mgr* mgr) {
  return ucm_swap_mode_exists_ret_value;
}

int ucm_enable_swap_mode(struct cras_use_case_mgr* mgr,
                         const char* node_name,
                         int enable) {
  ucm_enable_swap_mode_called++;
  return ucm_enable_swap_mode_ret_value;
}

int ucm_get_min_buffer_level(struct cras_use_case_mgr* mgr,
                             unsigned int* level) {
  *level = 0;
  return 0;
}

int ucm_get_use_software_volume(struct cras_use_case_mgr* mgr) {
  return 1;
}

char* ucm_get_hotword_models(struct cras_use_case_mgr* mgr) {
  return NULL;
}

int ucm_set_hotword_model(struct cras_use_case_mgr* mgr, const char* model) {
  return 0;
}

unsigned int ucm_get_dma_period_for_dev(struct cras_use_case_mgr* mgr,
                                        const char* dev) {
  ucm_get_dma_period_for_dev_called++;
  return ucm_get_dma_period_for_dev_ret;
}

int ucm_get_sample_rate_for_dev(struct cras_use_case_mgr* mgr,
                                const char* dev,
                                enum CRAS_STREAM_DIRECTION direction) {
  return -EINVAL;
}

int ucm_get_capture_chmap_for_dev(struct cras_use_case_mgr* mgr,
                                  const char* dev,
                                  int8_t* channel_layout) {
  return -EINVAL;
}

int ucm_get_playback_chmap_for_dev(struct cras_use_case_mgr* mgr,
                                   const char* dev,
                                   int8_t* channel_layout) {
  return -EINVAL;
}

int ucm_get_preempt_hotword(struct cras_use_case_mgr* mgr, const char* dev) {
  return 0;
}

int ucm_get_channels_for_dev(struct cras_use_case_mgr* mgr,
                             const char* dev,
                             enum CRAS_STREAM_DIRECTION direction,
                             size_t* channels) {
  return -EINVAL;
}

int ucm_node_noise_cancellation_exists(struct cras_use_case_mgr* mgr,
                                       const char* node_name) {
  // Assume that noise cancellation exists on internal microphone.
  if (!strcmp(node_name, INTERNAL_MICROPHONE)) {
    return 1;
  }
  return 0;
}

int ucm_enable_node_noise_cancellation(struct cras_use_case_mgr* mgr,
                                       const char* node_name,
                                       int enable) {
  return 0;
}

int ucm_node_echo_cancellation_exists(struct cras_use_case_mgr* mgr) {
  return ucm_node_echo_cancellation_exists_ret_value;
}

int ucm_node_spatial_audio_exists(struct cras_use_case_mgr* mgr) {
  return 0;
}

int ucm_enable_node_spatial_audio(struct cras_use_case_mgr* mgr, int enable) {
  return 0;
}

struct cras_volume_curve* cras_volume_curve_create_simple_step(long max_volume,
                                                               long range) {
  cras_volume_curve_create_simple_step_called++;
  cras_volume_curve_create_simple_step_max_volume = max_volume;
  cras_volume_curve_create_simple_step_range = range;
  return &default_curve;
}

struct cras_volume_curve* cras_volume_curve_create_default() {
  return &default_curve;
}

struct cras_volume_curve* cras_card_config_get_volume_curve_for_control(
    const struct cras_card_config* card_config,
    const char* control_name) {
  VolCurveMap::iterator it;
  cras_card_config_get_volume_curve_for_control_called++;
  if (!control_name) {
    return NULL;
  }
  it = cras_card_config_get_volume_curve_vals.find(control_name);
  if (it == cras_card_config_get_volume_curve_vals.end()) {
    return NULL;
  }
  return it->second;
}

void cras_iodev_free_format(struct cras_iodev* iodev) {}

int cras_iodev_set_format(struct cras_iodev* iodev,
                          const struct cras_audio_format* fmt) {
  fake_format = (struct cras_audio_format*)calloc(1, sizeof(cras_audio_format));
  // Copy the content of format from fmt into format of iodev.
  memcpy(fake_format, fmt, sizeof(cras_audio_format));
  iodev->format = fake_format;
  return 0;
}

struct audio_thread* audio_thread_create() {
  return reinterpret_cast<audio_thread*>(0x323);
}

void audio_thread_destroy(audio_thread* thread) {}

void cras_iodev_update_dsp(struct cras_iodev* iodev) {
  cras_iodev_update_dsp_called++;
  cras_iodev_update_dsp_name = iodev->dsp_name ?: "";
}

void cras_iodev_set_node_plugged(struct cras_ionode* ionode, int plugged) {
  cras_iodev_set_node_plugged_called++;
  cras_iodev_set_node_plugged_ionode = ionode;
  cras_iodev_set_node_plugged_value = plugged;
  if (ionode) {
    ionode->plugged = plugged;
  }
}

void cras_iodev_add_node(struct cras_iodev* iodev, struct cras_ionode* node) {
  cras_iodev_add_node_called++;
  DL_APPEND(iodev->nodes, node);
}

void cras_iodev_rm_node(struct cras_iodev* iodev, struct cras_ionode* node) {
  DL_DELETE(iodev->nodes, node);
}

void cras_iodev_set_active_node(struct cras_iodev* iodev,
                                struct cras_ionode* node) {
  iodev->active_node = node;
}

void cras_iodev_free_resources(struct cras_iodev* iodev) {
  if (iodev) {
    free(iodev->dsp_offload_map);
    iodev->dsp_offload_map = nullptr;
  }
  cras_iodev_free_resources_called++;
}

void cras_alsa_jack_update_monitor_name(const struct cras_alsa_jack* jack,
                                        char* name_buf,
                                        unsigned int buf_size) {
  if (cras_alsa_jack_update_monitor_fake_name) {
    strcpy(name_buf, cras_alsa_jack_update_monitor_fake_name);
  }
}

uint32_t cras_alsa_jack_get_monitor_stable_id(const struct cras_alsa_jack* jack,
                                              const char* monitor_name,
                                              uint32_t salt) {
  return 0;
}

void cras_alsa_jack_update_node_type(const struct cras_alsa_jack* jack,
                                     enum CRAS_NODE_TYPE* type) {
  cras_alsa_jack_update_node_type_called++;
}

const char* cras_alsa_jack_get_ucm_device(const struct cras_alsa_jack* jack) {
  return NULL;
}

void ucm_disable_all_hotword_models(struct cras_use_case_mgr* mgr) {}

int ucm_enable_hotword_model(struct cras_use_case_mgr* mgr) {
  return 0;
}

int ucm_get_default_node_gain(struct cras_use_case_mgr* mgr,
                              const char* dev,
                              long* gain) {
  if (ucm_get_default_node_gain_values.find(dev) ==
      ucm_get_default_node_gain_values.end()) {
    return 1;
  }

  *gain = ucm_get_default_node_gain_values[dev];
  return 0;
}

int ucm_get_latency_offset_ms(struct cras_use_case_mgr* mgr,
                              const char* name,
                              int* latency) {
  return -ENOENT;
}

int ucm_get_intrinsic_sensitivity(struct cras_use_case_mgr* mgr,
                                  const char* dev,
                                  long* vol) {
  if (ucm_get_intrinsic_sensitivity_values.find(dev) ==
      ucm_get_intrinsic_sensitivity_values.end()) {
    return 1;
  }

  *vol = ucm_get_intrinsic_sensitivity_values[dev];
  return 0;
}

int ucm_enable_node_echo_cancellation(struct cras_use_case_mgr* mgr,
                                      int enable) {
  return 0;
}

int ucm_enable_node_noise_suppression(struct cras_use_case_mgr* mgr,
                                      int enable) {
  return 0;
}

int ucm_enable_node_gain_control(struct cras_use_case_mgr* mgr, int enable) {
  return 0;
}

void cras_iodev_init_audio_area(struct cras_iodev* iodev) {}

void cras_iodev_free_audio_area(struct cras_iodev* iodev) {}

int cras_iodev_reset_rate_estimator(const struct cras_iodev* iodev) {
  cras_iodev_reset_rate_estimator_called++;
  return 0;
}

int cras_iodev_frames_queued(struct cras_iodev* iodev,
                             struct timespec* tstamp) {
  clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
  return cras_iodev_frames_queued_ret;
}

int cras_iodev_buffer_avail(struct cras_iodev* iodev, unsigned hw_level) {
  return cras_iodev_buffer_avail_ret;
}

int cras_iodev_fill_odev_zeros(struct cras_iodev* odev,
                               unsigned int frames,
                               bool processing) {
  cras_iodev_fill_odev_zeros_called++;
  cras_iodev_fill_odev_zeros_frames = frames;
  return (int)frames;
}

void cras_audio_area_config_buf_pointers(struct cras_audio_area* area,
                                         const struct cras_audio_format* fmt,
                                         uint8_t* base_buffer) {}

void audio_thread_add_events_callback(int fd,
                                      thread_callback cb,
                                      void* data,
                                      int events) {
  audio_thread_cb = cb;
  audio_thread_cb_data = data;
}

void audio_thread_rm_callback(int fd) {}

int audio_thread_rm_callback_sync(struct audio_thread* thread, int fd) {
  return 0;
}

int cras_hotword_send_triggered_msg() {
  hotword_send_triggered_msg_called++;
  return 0;
}

int snd_pcm_poll_descriptors_count(snd_pcm_t* pcm) {
  return 1;
}

int snd_pcm_poll_descriptors(snd_pcm_t* pcm,
                             struct pollfd* pfds,
                             unsigned int space) {
  if (space >= 1) {
    pfds[0].events = POLLIN;
    pfds[0].fd = 99;
  }
  return 0;
}

int is_utf8_string(const char* string) {
  return is_utf8_string_ret_value;
}

int cras_alsa_mmap_get_whole_buffer(snd_pcm_t* handle, uint8_t** dst) {
  snd_pcm_uframes_t offset, frames;

  cras_alsa_mmap_get_whole_buffer_called++;
  return cras_alsa_mmap_begin(handle, 0, dst, &offset, &frames);
}

int cras_alsa_resume_appl_ptr(snd_pcm_t* handle,
                              snd_pcm_uframes_t ahead,
                              int* actual_appl_ptr_displacement) {
  cras_alsa_resume_appl_ptr_called++;
  cras_alsa_resume_appl_ptr_ahead = ahead;
  if (actual_appl_ptr_displacement) {
    *actual_appl_ptr_displacement = ahead;
  }
  return 0;
}

int cras_iodev_default_no_stream_playback(struct cras_iodev* odev, int enable) {
  return 0;
}

int cras_iodev_output_underrun(struct cras_iodev* odev,
                               unsigned int hw_level,
                               unsigned int frames_written) {
  return odev->output_underrun(odev);
}

enum CRAS_IODEV_STATE cras_iodev_state(const struct cras_iodev* iodev) {
  return iodev->state;
}

int cras_iodev_dsp_set_swap_mode_for_node(struct cras_iodev* iodev,
                                          struct cras_ionode* node,
                                          int enable) {
  cras_iodev_dsp_set_swap_mode_for_node_called++;
  return 0;
}

void cras_iodev_update_underrun_duration(struct cras_iodev* iodev,
                                         unsigned frames) {
  cras_iodev_update_underrun_duration_called++;
}

struct cras_ramp* cras_ramp_create() {
  return (struct cras_ramp*)0x1;
}

int cras_server_metrics_device_noise_cancellation_status(
    struct cras_iodev* iodev,
    int status) {
  return 0;
}

int cras_system_state_get_input_nodes(const struct cras_ionode_info** nodes) {
  return 0;
}

//  From librt.
int clock_gettime(clockid_t clk_id, struct timespec* tp) {
  tp->tv_sec = clock_gettime_retspec.tv_sec;
  tp->tv_nsec = clock_gettime_retspec.tv_nsec;
  return 0;
}

void cras_board_config_get(const char* config_path,
                           struct cras_board_config* board_config) {
  *board_config = fake_board_config;
}

int cras_system_get_speaker_output_latency_offset_ms() {
  return 0;
}

bool cras_iodev_list_get_dsp_nc_allowed() {
  return false;
}

void cras_iodev_stream_offset_reset_all(struct cras_iodev* iodev) {}

int cras_dsp_offload_create_map(struct dsp_offload_map** offload_map,
                                const struct cras_ionode* node) {
  if (node->type == CRAS_NODE_TYPE_INTERNAL_SPEAKER) {
    struct dsp_offload_map* alloc_map =
        (struct dsp_offload_map*)calloc(1, sizeof(*alloc_map));
    *offload_map = alloc_map;
    return 0;
  }
  return -EINVAL;
}

void cras_dsp_context_set_offload_map(struct cras_dsp_context* ctx,
                                      struct dsp_offload_map* offload_map) {}
}  // extern "C"

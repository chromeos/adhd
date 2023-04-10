// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <map>
#include <stdio.h>
#include <syslog.h>
#include <vector>

#include "cras_util.h"

extern "C" {

#include "cras/src/server/cras_alsa_io_ops.h"
#include "cras/src/server/cras_alsa_mixer.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_system_state.h"
#include "cras_shm.h"
#include "cras_types.h"

//  Include C file to test static functions.
#include "cras/src/server/cras_alsa_usb_io.c"
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
static struct mixer_control* cras_alsa_jack_get_mixer_output_ret;
static struct mixer_control* cras_alsa_jack_get_mixer_input_ret;
static size_t cras_alsa_mixer_get_output_volume_curve_called;
typedef std::map<const struct mixer_control*, std::string> ControlNameMap;
static ControlNameMap cras_alsa_mixer_get_control_name_values;
static size_t cras_alsa_mixer_get_control_name_called;
static size_t cras_alsa_jack_list_create_called;
static size_t cras_alsa_jack_list_find_jacks_by_name_matching_called;
static size_t cras_alsa_jack_list_add_jack_for_section_called;
static struct cras_alsa_jack*
    cras_alsa_jack_list_add_jack_for_section_result_jack;
static size_t cras_alsa_jack_list_destroy_called;
static int cras_alsa_jack_list_has_hctl_jacks_return_val;
static jack_state_change_callback* cras_alsa_jack_list_create_cb;
static void* cras_alsa_jack_list_create_cb_data;
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
static unsigned display_rotation;
static bool sys_get_noise_cancellation_supported_return_value;
static int sys_aec_on_dsp_supported_return_value;
static int ucm_node_echo_cancellation_exists_ret_value;
static int sys_get_max_internal_speaker_channels_called;
static int sys_get_max_internal_speaker_channels_return_value;
static int sys_get_max_headphone_channels_called = 0;
static int sys_get_max_headphone_channels_return_value = 2;
static int cras_iodev_update_underrun_duration_called = 0;

void cras_dsp_set_variable_integer(struct cras_dsp_context* ctx,
                                   const char* key,
                                   int value) {
  if (!strcmp(key, "display_rotation")) {
    display_rotation = value;
  }
}

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
  cras_alsa_jack_get_mixer_output_ret = NULL;
  cras_alsa_jack_get_mixer_input_ret = NULL;
  cras_alsa_mixer_get_control_name_values.clear();
  cras_alsa_mixer_get_control_name_called = 0;
  cras_alsa_jack_list_create_called = 0;
  cras_alsa_jack_list_find_jacks_by_name_matching_called = 0;
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
  display_rotation = 0;
  sys_get_noise_cancellation_supported_return_value = 0;
  sys_aec_on_dsp_supported_return_value = 0;
  ucm_node_echo_cancellation_exists_ret_value = 0;
  sys_get_max_internal_speaker_channels_called = 0;
  sys_get_max_internal_speaker_channels_return_value = 2;
  sys_get_max_headphone_channels_called = 0;
  sys_get_max_headphone_channels_return_value = 2;
  cras_iodev_update_underrun_duration_called = 0;
}

static long fake_get_dBFS(const struct cras_volume_curve* curve,
                          size_t volume) {
  fake_get_dBFS_volume_curve_val = curve;
  return (volume - 100) * 100;
}

static cras_volume_curve default_curve = {
    .get_dBFS = fake_get_dBFS,
};

static struct cras_iodev* cras_alsa_usb_iodev_create_with_default_parameters(
    size_t card_index,
    const char* dev_id,
    enum CRAS_ALSA_CARD_TYPE card_type,
    int is_first,
    struct cras_alsa_mixer* mixer,
    struct cras_card_config* config,
    struct cras_use_case_mgr* ucm,
    enum CRAS_STREAM_DIRECTION direction) {
  return cras_alsa_usb_iodev_create(card_index, test_card_name, 0,
                                    test_pcm_name, test_dev_name, dev_id,
                                    card_type, is_first, mixer, config, ucm,
                                    fake_hctl, direction, 0, 0, (char*)"123");
}

TEST(AlsaIoInit, DefaultNodeUSBCard) {
  struct alsa_usb_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_usb_io*)cras_alsa_usb_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_USB, 1, fake_mixer, fake_config, NULL,
      CRAS_STREAM_OUTPUT);
  ASSERT_EQ(0,
            cras_alsa_usb_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);
  EXPECT_EQ(2, cras_alsa_mixer_get_playback_dBFS_range_called);
  EXPECT_EQ(1, cras_volume_curve_create_simple_step_called);
  EXPECT_EQ(cras_alsa_mixer_get_playback_dBFS_range_max,
            cras_volume_curve_create_simple_step_max_volume);
  EXPECT_EQ((cras_alsa_mixer_get_playback_dBFS_range_max -
             cras_alsa_mixer_get_playback_dBFS_range_min),
            cras_volume_curve_create_simple_step_range);
  ASSERT_STREQ(DEFAULT, aio->base.active_node->name);
  ASSERT_EQ(1, aio->base.active_node->plugged);
  EXPECT_EQ(1, cras_iodev_set_node_plugged_called);
  EXPECT_EQ(2, cras_alsa_mixer_get_playback_step_called);
  cras_alsa_usb_iodev_destroy((struct cras_iodev*)aio);

  aio = (struct alsa_usb_io*)cras_alsa_usb_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_USB, 1, fake_mixer, fake_config, NULL,
      CRAS_STREAM_INPUT);
  ASSERT_EQ(0,
            cras_alsa_usb_iodev_legacy_complete_init((struct cras_iodev*)aio));
  EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);
  ASSERT_STREQ(DEFAULT, aio->base.active_node->name);
  ASSERT_EQ(1, aio->base.active_node->plugged);
  EXPECT_EQ(2, cras_iodev_set_node_plugged_called);

  // No extra gain applied.
  ASSERT_EQ(DEFAULT_CAPTURE_VOLUME_DBFS,
            aio->base.active_node->intrinsic_sensitivity);
  ASSERT_EQ(0, aio->base.active_node->capture_gain);
  cras_alsa_usb_iodev_destroy((struct cras_iodev*)aio);
}

TEST(AlsaIoInit, OpenCaptureSetCaptureGainWithDefaultUsbDevice) {
  struct cras_iodev* iodev;
  struct cras_audio_format format;

  iodev = cras_alsa_usb_iodev_create_with_default_parameters(
      0, NULL, ALSA_CARD_TYPE_USB, 0, fake_mixer, fake_config, NULL,
      CRAS_STREAM_INPUT);
  ASSERT_EQ(0, cras_alsa_usb_iodev_legacy_complete_init(iodev));

  format.frame_rate = 48000;
  format.num_channels = 1;
  cras_iodev_set_format(iodev, &format);

  iodev->active_node->intrinsic_sensitivity = DEFAULT_CAPTURE_VOLUME_DBFS;
  iodev->active_node->capture_gain = 0;

  ResetStubData();
  iodev->open_dev(iodev);
  iodev->configure_dev(iodev);

  // Not change mixer controls for USB devices without UCM config.
  EXPECT_EQ(0, alsa_mixer_set_capture_dBFS_called);

  cras_alsa_usb_iodev_destroy(iodev);
  free(fake_format);
}

TEST(AlsaIoInit, MaxSupportedChannels) {
  struct alsa_usb_io* aio;
  struct cras_alsa_mixer* const fake_mixer = (struct cras_alsa_mixer*)2;
  int i;

  // i = 0: cras_alsa_support_8_channels is false, support 2 channels only.
  // i = 1: cras_alsa_support_8_channels is true, support up to 8 channels.
  for (i = 0; i < 2; i++) {
    ResetStubData();
    cras_alsa_support_8_channels = (bool)i;

    aio =
        (struct alsa_usb_io*)cras_alsa_usb_iodev_create_with_default_parameters(
            0, test_dev_id, ALSA_CARD_TYPE_USB, 1, fake_mixer, fake_config,
            NULL, CRAS_STREAM_OUTPUT);
    ASSERT_EQ(
        0, cras_alsa_usb_iodev_legacy_complete_init((struct cras_iodev*)aio));
    // Call cras_alsa_fill_properties once on update_max_supported_channels.
    EXPECT_EQ(1, cras_alsa_fill_properties_called);
    uint32_t max_channels = (cras_alsa_support_8_channels) ? 8 : 2;
    EXPECT_EQ(max_channels, aio->base.info.max_supported_channels);
    cras_alsa_usb_iodev_destroy((struct cras_iodev*)aio);
    EXPECT_EQ(1, cras_iodev_free_resources_called);
  }
}
TEST(AlsaInitNode, SetNodeInitialState) {
  struct cras_ionode node;
  struct cras_iodev dev;

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Unknown");
  dev.direction = CRAS_STREAM_OUTPUT;
  usb_set_node_initial_state(&node);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_USB, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, INTERNAL_SPEAKER);
  dev.direction = CRAS_STREAM_OUTPUT;
  usb_set_node_initial_state(&node);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(node.plugged_time.tv_sec, 0);
  ASSERT_EQ(CRAS_NODE_TYPE_USB, node.type);
  ASSERT_EQ(NODE_POSITION_EXTERNAL, node.position);
}

TEST(AlsaInitNode, SetNodeInitialStateDropInvalidUTF8NodeName) {
  struct cras_ionode node;
  struct cras_iodev dev;

  memset(&dev, 0, sizeof(dev));
  memset(&node, 0, sizeof(node));
  node.dev = &dev;

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Something USB");
  // 0xfe can not appear in a valid UTF-8 string.
  node.name[0] = 0xfe;
  is_utf8_string_ret_value = 0;
  dev.direction = CRAS_STREAM_OUTPUT;
  usb_set_node_initial_state(&node);
  ASSERT_EQ(CRAS_NODE_TYPE_USB, node.type);
  ASSERT_STREQ(USB, node.name);
}
class NodeUSBCardSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    fake_mixer = (struct cras_alsa_mixer*)2;
    outputs = reinterpret_cast<struct mixer_control*>(0);
  }
  void CheckExpectBehaviorWithDifferentNumberOfVolumeStep(
      int control_volume_steps,
      int expect_output_node_volume_steps,
      int expect_enable_software_volume) {
    ResetStubData();
    cras_alsa_mixer_get_control_name_values[outputs] = HEADPHONE;
    cras_alsa_mixer_get_playback_step_values[outputs] = control_volume_steps;
    aio =
        (struct alsa_usb_io*)cras_alsa_usb_iodev_create_with_default_parameters(
            0, NULL, ALSA_CARD_TYPE_USB, 1, fake_mixer, fake_config, NULL,
            CRAS_STREAM_OUTPUT);
    ASSERT_EQ(
        0, cras_alsa_usb_iodev_legacy_complete_init((struct cras_iodev*)aio));
    EXPECT_EQ(2, cras_alsa_mixer_get_playback_step_called);
    EXPECT_EQ(expect_output_node_volume_steps,
              aio->base.active_node->number_of_volume_steps);
    EXPECT_EQ(expect_enable_software_volume,
              aio->base.active_node->software_volume_needed);
    cras_alsa_usb_iodev_destroy((struct cras_iodev*)aio);
  }
  void CheckVolumeCurveWithDifferentVolumeRange(
      long dBFS_range_max,
      long dBFS_range_min,
      int expect_enable_software_volume) {
    ResetStubData();
    cras_alsa_mixer_get_playback_dBFS_range_max = dBFS_range_max;
    cras_alsa_mixer_get_playback_dBFS_range_min = dBFS_range_min;
    cras_alsa_mixer_get_control_name_values[outputs] = HEADPHONE;
    aio =
        (struct alsa_usb_io*)cras_alsa_usb_iodev_create_with_default_parameters(
            0, NULL, ALSA_CARD_TYPE_USB, 1, fake_mixer, fake_config, NULL,
            CRAS_STREAM_OUTPUT);
    ASSERT_EQ(
        0, cras_alsa_usb_iodev_legacy_complete_init((struct cras_iodev*)aio));
    EXPECT_EQ(2, cras_card_config_get_volume_curve_for_control_called);
    EXPECT_EQ(2, cras_alsa_mixer_get_playback_dBFS_range_called);
    EXPECT_EQ(expect_enable_software_volume,
              aio->base.active_node->software_volume_needed);
    EXPECT_EQ(&default_curve, fake_get_dBFS_volume_curve_val);
    if (!expect_enable_software_volume) {
      EXPECT_EQ(cras_alsa_mixer_get_playback_dBFS_range_max,
                cras_volume_curve_create_simple_step_max_volume);
      EXPECT_EQ((cras_alsa_mixer_get_playback_dBFS_range_max -
                 cras_alsa_mixer_get_playback_dBFS_range_min),
                cras_volume_curve_create_simple_step_range);
      EXPECT_EQ(1, cras_volume_curve_create_simple_step_called);
    } else {
      EXPECT_EQ(0, cras_volume_curve_create_simple_step_called);
    }
    cras_alsa_usb_iodev_destroy((struct cras_iodev*)aio);
  }
  struct alsa_usb_io* aio;
  struct cras_alsa_mixer* fake_mixer;
  struct mixer_control* outputs;
};

TEST_F(NodeUSBCardSuite, NumberOfVolumeStep) {
  /* For number_of_volume_steps < 10, set number_of_volume_steps = 25 and enable
   * software_volume
   */
  CheckExpectBehaviorWithDifferentNumberOfVolumeStep(0, 25, 1);
  /* For number_of_volume_steps >= 10 && number_of_volume_steps <= 25, set
   * ionode same as number_of_volume_steps mixer_control reported
   */
  CheckExpectBehaviorWithDifferentNumberOfVolumeStep(10, 10, 0);
  /* For number_of_volume_steps >= 10 && number_of_volume_steps <= 25, set
   * ionode same as number_of_volume_steps mixer_control reported
   */
  CheckExpectBehaviorWithDifferentNumberOfVolumeStep(15, 15, 0);
  /* For number_of_volume_steps >= 10 && number_of_volume_steps <= 25, set
   * ionode same as number_of_volume_steps mixer_control reported
   */
  CheckExpectBehaviorWithDifferentNumberOfVolumeStep(25, 25, 0);
  /* For number_of_volume_steps >= 25 set set number_of_volume_steps = 25
   */
  CheckExpectBehaviorWithDifferentNumberOfVolumeStep(50, 25, 0);
}

TEST_F(NodeUSBCardSuite, VolumeRange) {
  /* For USB devices 5.00 dB - 200.00 dB will be considered the normal volume
   * range. If the range reported by the USB device is outside this range,
   * fallback to software volume and use default volume curve.
   */

  // lower that 5.00 dBFS, use software volume and default volume curve
  CheckVolumeCurveWithDifferentVolumeRange(0, db_to_alsa_db(-2), 1);
  // 5.00 dBFS, use hardware volume and custom volume curve
  CheckVolumeCurveWithDifferentVolumeRange(0, db_to_alsa_db(-5), 0);
  // 20.00 dBFS, use hardware volume and custom volume curve
  CheckVolumeCurveWithDifferentVolumeRange(0, db_to_alsa_db(-20), 0);
  // 200.00 dBFS, use hardware volume and custom volume curve
  CheckVolumeCurveWithDifferentVolumeRange(0, db_to_alsa_db(-200), 0);
  // 999999.00 dBFS, use software volume and default volume curve
  CheckVolumeCurveWithDifferentVolumeRange(0, db_to_alsa_db(-999999), 1);
}

//  Test free run.
class USBFreeRunTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    memset(&aio, 0, sizeof(aio));
    fmt_.format = SND_PCM_FORMAT_S16_LE;
    fmt_.frame_rate = 48000;
    fmt_.num_channels = 2;
    aio.base.frames_queued = usb_frames_queued;
    aio.base.output_underrun = usb_alsa_output_underrun;
    aio.base.direction = CRAS_STREAM_OUTPUT;
    aio.base.format = &fmt_;
    aio.base.buffer_size = BUFFER_SIZE;
    aio.base.min_cb_level = 240;
    aio.base.min_buffer_level = 0;
    aio.filled_zeros_for_draining = 0;
    cras_alsa_mmap_begin_buffer = (uint8_t*)calloc(
        BUFFER_SIZE * 2 * 2, sizeof(*cras_alsa_mmap_begin_buffer));
    memset(cras_alsa_mmap_begin_buffer, 0xff,
           sizeof(*cras_alsa_mmap_begin_buffer));
  }

  virtual void TearDown() { free(cras_alsa_mmap_begin_buffer); }

  struct alsa_usb_io aio;
  struct cras_audio_format fmt_;
};

TEST_F(USBFreeRunTestSuite, OutputUnderrun) {
  int rc;
  int16_t* zeros;
  snd_pcm_uframes_t offset;

  // Ask alsa_io to handle output underrun.
  rc = usb_alsa_output_underrun(&aio.base);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, cras_iodev_update_underrun_duration_called);

  // mmap buffer should be filled with zeros.
  zeros = (int16_t*)calloc(BUFFER_SIZE * 2, sizeof(*zeros));
  EXPECT_EQ(0, memcmp(zeros, cras_alsa_mmap_begin_buffer, BUFFER_SIZE * 2 * 2));

  // appl_ptr should be moved to min_buffer_level + 1.5 * min_cb_level ahead of
  // hw_ptr.
  offset = aio.base.min_buffer_level + aio.base.min_cb_level +
           aio.base.min_cb_level / 2;
  EXPECT_EQ(1, cras_alsa_resume_appl_ptr_called);
  EXPECT_EQ(offset, cras_alsa_resume_appl_ptr_ahead);

  free(zeros);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  openlog(NULL, LOG_PERROR, LOG_USER);
  return RUN_ALL_TESTS();
}

//  Stubs

extern "C" {
int cras_iodev_list_add_output(struct cras_iodev* output) {
  return 0;
}
int cras_iodev_list_rm_output(struct cras_iodev* dev) {
  return 0;
}

int cras_iodev_list_add_input(struct cras_iodev* input) {
  return 0;
}
int cras_iodev_list_rm_input(struct cras_iodev* dev) {
  return 0;
}

char* cras_iodev_list_get_hotword_models(cras_node_id_t node_id) {
  return NULL;
}

int cras_iodev_list_set_hotword_model(cras_node_id_t node_id,
                                      const char* model_name) {
  return 0;
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

bool cras_system_get_dsp_noise_cancellation_supported() {
  return sys_get_noise_cancellation_supported_return_value;
}

bool cras_system_get_ap_noise_cancellation_supported() {
  return false;
}

bool cras_system_get_noise_cancellation_enabled() {
  return false;
}

int cras_system_aec_on_dsp_supported() {
  return sys_aec_on_dsp_supported_return_value;
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
  cras_alsa_jack_list_create_cb = cb;
  cras_alsa_jack_list_create_cb_data = cb_data;
  return (struct cras_alsa_jack_list*)0xfee;
}

int cras_alsa_jack_list_find_jacks_by_name_matching(
    struct cras_alsa_jack_list* jack_list) {
  cras_alsa_jack_list_find_jacks_by_name_matching_called++;
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

struct mixer_control* cras_alsa_jack_get_mixer_output(
    const struct cras_alsa_jack* jack) {
  return cras_alsa_jack_get_mixer_output_ret;
}

struct mixer_control* cras_alsa_jack_get_mixer_input(
    const struct cras_alsa_jack* jack) {
  return cras_alsa_jack_get_mixer_input_ret;
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

unsigned int ucm_get_disable_software_volume(struct cras_use_case_mgr* mgr) {
  return 0;
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

void cras_iodev_init_audio_area(struct cras_iodev* iodev, int num_channels) {}

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
                               bool underrun) {
  cras_iodev_fill_odev_zeros_called++;
  cras_iodev_fill_odev_zeros_frames = frames;
  return 0;
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

int cras_iodev_dsp_set_display_rotation_for_node(
    struct cras_iodev* iodev,
    struct cras_ionode* node,
    enum CRAS_SCREEN_ROTATION rotation) {
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

}  // extern "C"

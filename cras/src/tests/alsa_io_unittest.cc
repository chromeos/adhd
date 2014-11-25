// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {

#include "cras_iodev.h"
#include "cras_shm.h"
#include "cras_system_state.h"
#include "cras_types.h"
#include "cras_alsa_mixer.h"

//  Include C file to test static functions.
#include "cras_alsa_io.c"
}

//  Data for simulating functions stubbed below.
static int cras_alsa_open_called;
static int cras_iodev_append_stream_ret;
static int cras_alsa_get_avail_frames_ret;
static int cras_alsa_get_avail_frames_avail;
static int cras_alsa_start_called;
static uint8_t *cras_alsa_mmap_begin_buffer;
static size_t cras_alsa_mmap_begin_frames;
static size_t cras_alsa_fill_properties_called;
static size_t alsa_mixer_set_dBFS_called;
static int alsa_mixer_set_dBFS_value;
static const struct cras_alsa_mixer_output *alsa_mixer_set_dBFS_output;
static size_t alsa_mixer_set_capture_dBFS_called;
static int alsa_mixer_set_capture_dBFS_value;
static const struct mixer_volume_control *alsa_mixer_set_capture_dBFS_input;
static const struct mixer_volume_control
    *cras_alsa_mixer_get_minimum_capture_gain_mixer_input;
static const struct mixer_volume_control
    *cras_alsa_mixer_get_maximum_capture_gain_mixer_input;
static size_t cras_alsa_mixer_list_outputs_called;
static size_t cras_alsa_mixer_list_outputs_device_value;
static size_t sys_get_volume_called;
static size_t sys_get_volume_return_value;
static size_t sys_get_capture_gain_called;
static long sys_get_capture_gain_return_value;
static size_t alsa_mixer_set_mute_called;
static int alsa_mixer_set_mute_value;
static const struct cras_alsa_mixer_output *alsa_mixer_set_mute_output;
static size_t alsa_mixer_set_capture_mute_called;
static int alsa_mixer_set_capture_mute_value;
static size_t sys_get_mute_called;
static int sys_get_mute_return_value;
static size_t sys_get_capture_mute_called;
static int sys_get_capture_mute_return_value;
static struct cras_alsa_mixer *fake_mixer = (struct cras_alsa_mixer *)1;
static struct cras_alsa_mixer_output **cras_alsa_mixer_list_outputs_outputs;
static size_t cras_alsa_mixer_list_outputs_outputs_length;
static size_t cras_alsa_mixer_set_output_active_state_called;
static std::vector<struct cras_alsa_mixer_output *>
    cras_alsa_mixer_set_output_active_state_outputs;
static std::vector<int> cras_alsa_mixer_set_output_active_state_values;
static size_t cras_alsa_mixer_default_volume_curve_called;
static cras_volume_curve *fake_curve;
static cras_audio_format *fake_format;
static size_t sys_set_volume_limits_called;
static size_t sys_set_capture_gain_limits_called;
static size_t cras_alsa_mixer_get_minimum_capture_gain_called;
static size_t cras_alsa_mixer_get_maximum_capture_gain_called;
static size_t cras_alsa_jack_list_create_called;
static size_t cras_alsa_jack_list_destroy_called;
static jack_state_change_callback *cras_alsa_jack_list_create_cb;
static void *cras_alsa_jack_list_create_cb_data;
static char test_card_name[] = "TestCard";
static char test_dev_name[] = "TestDev";
static size_t cras_iodev_set_node_attr_called;
static enum ionode_attr cras_iodev_set_node_attr_attr;
static int cras_iodev_set_node_attr_value;
static size_t cras_iodev_list_node_selected_called;
static unsigned cras_alsa_jack_enable_ucm_called;
static size_t cras_iodev_update_dsp_called;
static const char *cras_iodev_update_dsp_name;
static size_t ucm_get_dsp_name_default_called;
static const char *ucm_get_dsp_name_default_value;
static size_t cras_alsa_jack_get_dsp_name_called;
static const char *cras_alsa_jack_get_dsp_name_value;
static size_t cras_iodev_free_resources_called;
static size_t cras_alsa_jack_exists_called;
static const char *cras_alsa_jack_exists_match;
static size_t cras_alsa_jack_update_node_type_called;
static int ucm_swap_mode_exists_ret_value;
static int ucm_enable_swap_mode_ret_value;
static size_t ucm_enable_swap_mode_called;

void ResetStubData() {
  cras_alsa_open_called = 0;
  cras_iodev_append_stream_ret = 0;
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = 0;
  cras_alsa_start_called = 0;
  cras_alsa_fill_properties_called = 0;
  sys_get_volume_called = 0;
  sys_get_capture_gain_called = 0;
  alsa_mixer_set_dBFS_called = 0;
  alsa_mixer_set_capture_dBFS_called = 0;
  sys_get_mute_called = 0;
  sys_get_capture_mute_called = 0;
  alsa_mixer_set_mute_called = 0;
  alsa_mixer_set_capture_mute_called = 0;
  cras_alsa_mixer_list_outputs_called = 0;
  cras_alsa_mixer_list_outputs_outputs_length = 0;
  cras_alsa_mixer_set_output_active_state_called = 0;
  cras_alsa_mixer_set_output_active_state_outputs.clear();
  cras_alsa_mixer_set_output_active_state_values.clear();
  cras_alsa_mixer_default_volume_curve_called = 0;
  sys_set_volume_limits_called = 0;
  sys_set_capture_gain_limits_called = 0;
  cras_alsa_mixer_get_minimum_capture_gain_called = 0;
  cras_alsa_mixer_get_maximum_capture_gain_called = 0;
  cras_alsa_jack_list_create_called = 0;
  cras_alsa_jack_list_destroy_called = 0;
  cras_iodev_set_node_attr_called = 0;
  cras_iodev_list_node_selected_called = 0;
  cras_alsa_jack_enable_ucm_called = 0;
  cras_iodev_update_dsp_called = 0;
  cras_iodev_update_dsp_name = 0;
  ucm_get_dsp_name_default_called = 0;
  ucm_get_dsp_name_default_value = NULL;
  cras_alsa_jack_get_dsp_name_called = 0;
  cras_alsa_jack_get_dsp_name_value = NULL;
  cras_iodev_free_resources_called = 0;
  cras_alsa_jack_exists_called = 0;
  cras_alsa_jack_exists_match = NULL;
  cras_alsa_jack_update_node_type_called = 0;
  ucm_swap_mode_exists_ret_value = 0;
  ucm_enable_swap_mode_ret_value = 0;
  ucm_enable_swap_mode_called = 0;
}

static long fake_get_dBFS(const cras_volume_curve *curve, size_t volume)
{
  return (volume - 100) * 100;
}

namespace {

TEST(AlsaIoInit, InitializeInvalidDirection) {
  struct alsa_io *aio;

  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, NULL,
                                            CRAS_NUM_DIRECTIONS);
  ASSERT_EQ(aio, (void *)NULL);
}

TEST(AlsaIoInit, InitializePlayback) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_fill_properties_called);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);
  EXPECT_EQ(0, cras_alsa_mixer_list_outputs_device_value);
  EXPECT_EQ(0, strncmp(test_card_name,
                       aio->base.info.name,
		       strlen(test_card_name)));
  EXPECT_EQ(0, ucm_get_dsp_name_default_called);
  EXPECT_EQ(NULL, cras_iodev_update_dsp_name);

  alsa_iodev_destroy((struct cras_iodev *)aio);
  EXPECT_EQ(1, cras_iodev_free_resources_called);
}

TEST(AlsaIoInit, DefaultNodeInternalCard) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_OUTPUT);

  ASSERT_STREQ("(default)", aio->base.active_node->name);
  ASSERT_EQ(1, aio->base.active_node->plugged);
  alsa_iodev_destroy((struct cras_iodev *)aio);

  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 1,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_OUTPUT);

  ASSERT_STREQ("Speaker", aio->base.active_node->name);
  ASSERT_EQ(1, aio->base.active_node->plugged);
  alsa_iodev_destroy((struct cras_iodev *)aio);

  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_INPUT);

  ASSERT_STREQ("(default)", aio->base.active_node->name);
  ASSERT_EQ(1, aio->base.active_node->plugged);
  ASSERT_EQ(0, cras_alsa_jack_exists_called);
  alsa_iodev_destroy((struct cras_iodev *)aio);

  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 1,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_INPUT);

  ASSERT_STREQ("Internal Mic", aio->base.active_node->name);
  ASSERT_EQ(1, aio->base.active_node->plugged);
  ASSERT_EQ(1, cras_alsa_jack_exists_called);
  alsa_iodev_destroy((struct cras_iodev *)aio);


  cras_alsa_jack_exists_match = "Speaker Phantom Jack";
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 1,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_INPUT);

  ASSERT_STREQ("(default)", aio->base.active_node->name);
  ASSERT_EQ(1, aio->base.active_node->plugged);
  ASSERT_EQ(3, cras_alsa_jack_exists_called);
  alsa_iodev_destroy((struct cras_iodev *)aio);

}

TEST(AlsaIoInit, DefaultNodeUSBCard) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_USB, 1,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_OUTPUT);

  ASSERT_STREQ("(default)", aio->base.active_node->name);
  ASSERT_EQ(1, aio->base.active_node->plugged);
  EXPECT_EQ(1, cras_iodev_set_node_attr_called);
  EXPECT_EQ(IONODE_ATTR_PLUGGED, cras_iodev_set_node_attr_attr);
  EXPECT_EQ(1, cras_iodev_set_node_attr_value);
  alsa_iodev_destroy((struct cras_iodev *)aio);

  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_USB, 1,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_INPUT);

  ASSERT_STREQ("(default)", aio->base.active_node->name);
  ASSERT_EQ(1, aio->base.active_node->plugged);
  EXPECT_EQ(2, cras_iodev_set_node_attr_called);
  EXPECT_EQ(IONODE_ATTR_PLUGGED, cras_iodev_set_node_attr_attr);
  EXPECT_EQ(1, cras_iodev_set_node_attr_value);
  alsa_iodev_destroy((struct cras_iodev *)aio);
}

TEST(AlsaIoInit, OpenPlayback) {
  struct cras_iodev *iodev;
  struct cras_audio_format *format = NULL;

  ResetStubData();
  iodev = alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                            ALSA_CARD_TYPE_INTERNAL, 0,
                            fake_mixer, NULL,
                            CRAS_STREAM_OUTPUT);

  cras_iodev_set_format(iodev, format);
  fake_curve =
      static_cast<struct cras_volume_curve *>(calloc(1, sizeof(*fake_curve)));
  fake_curve->get_dBFS = fake_get_dBFS;

  iodev->open_dev(iodev);
  EXPECT_EQ(1, cras_alsa_open_called);
  EXPECT_EQ(1, sys_set_volume_limits_called);
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  EXPECT_EQ(0, cras_alsa_start_called);
  EXPECT_EQ(1, cras_iodev_list_node_selected_called);
  EXPECT_EQ(0, cras_iodev_set_node_attr_called);

  alsa_iodev_destroy(iodev);
  free(fake_curve);
  fake_curve = NULL;
  free(fake_format);
}

TEST(AlsaIoInit, UsbCardAutoPlug) {
  struct cras_iodev *iodev;

  ResetStubData();
  iodev = alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                            ALSA_CARD_TYPE_INTERNAL, 1,
                            fake_mixer, NULL,
                            CRAS_STREAM_OUTPUT);
  EXPECT_EQ(0, cras_iodev_set_node_attr_called);
  alsa_iodev_destroy(iodev);

  ResetStubData();
  iodev = alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                            ALSA_CARD_TYPE_USB, 0,
                            fake_mixer, NULL,
                            CRAS_STREAM_OUTPUT);
  EXPECT_EQ(0, cras_iodev_set_node_attr_called);
  alsa_iodev_destroy(iodev);

  ResetStubData();
  iodev = alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                            ALSA_CARD_TYPE_USB, 1,
                            fake_mixer, NULL,
                            CRAS_STREAM_OUTPUT);
  // Should assume USB devs are plugged when they appear.
  EXPECT_EQ(1, cras_iodev_set_node_attr_called);
  EXPECT_EQ(IONODE_ATTR_PLUGGED, cras_iodev_set_node_attr_attr);
  EXPECT_EQ(1, cras_iodev_set_node_attr_value);
  alsa_iodev_destroy(iodev);
}

TEST(AlsaIoInit, RouteBasedOnJackCallback) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_fill_properties_called);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);
  EXPECT_EQ(0, cras_alsa_mixer_list_outputs_device_value);
  EXPECT_EQ(1, cras_alsa_jack_list_create_called);
  EXPECT_EQ(1, cras_iodev_list_node_selected_called);

  fake_curve =
    static_cast<struct cras_volume_curve *>(calloc(1, sizeof(*fake_curve)));
  fake_curve->get_dBFS = fake_get_dBFS;

  cras_alsa_jack_list_create_cb(NULL, 1, cras_alsa_jack_list_create_cb_data);
  EXPECT_EQ(1, cras_iodev_set_node_attr_called);
  EXPECT_EQ(IONODE_ATTR_PLUGGED, cras_iodev_set_node_attr_attr);
  EXPECT_EQ(1, cras_iodev_set_node_attr_value);
  cras_alsa_jack_list_create_cb(NULL, 0, cras_alsa_jack_list_create_cb_data);
  EXPECT_EQ(2, cras_iodev_set_node_attr_called);
  EXPECT_EQ(IONODE_ATTR_PLUGGED, cras_iodev_set_node_attr_attr);
  EXPECT_EQ(0, cras_iodev_set_node_attr_value);

  alsa_iodev_destroy((struct cras_iodev *)aio);
  EXPECT_EQ(1, cras_alsa_jack_list_destroy_called);
  free(fake_curve);
  fake_curve = NULL;
}

TEST(AlsaIoInit, RouteBasedOnInputJackCallback) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_INPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_CAPTURE, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_fill_properties_called);
  EXPECT_EQ(1, cras_alsa_jack_list_create_called);
  EXPECT_EQ(1, cras_iodev_list_node_selected_called);

  fake_curve =
    static_cast<struct cras_volume_curve *>(calloc(1, sizeof(*fake_curve)));
  fake_curve->get_dBFS = fake_get_dBFS;

  cras_alsa_jack_list_create_cb(NULL, 1, cras_alsa_jack_list_create_cb_data);
  EXPECT_EQ(1, cras_iodev_set_node_attr_called);
  EXPECT_EQ(IONODE_ATTR_PLUGGED, cras_iodev_set_node_attr_attr);
  EXPECT_EQ(1, cras_iodev_set_node_attr_value);
  cras_alsa_jack_list_create_cb(NULL, 0, cras_alsa_jack_list_create_cb_data);
  EXPECT_EQ(2, cras_iodev_set_node_attr_called);
  EXPECT_EQ(IONODE_ATTR_PLUGGED, cras_iodev_set_node_attr_attr);
  EXPECT_EQ(0, cras_iodev_set_node_attr_value);

  alsa_iodev_destroy((struct cras_iodev *)aio);
  EXPECT_EQ(1, cras_alsa_jack_list_destroy_called);
  free(fake_curve);
  fake_curve = NULL;
}

TEST(AlsaIoInit, InitializeCapture) {
  struct alsa_io *aio;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_INPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_CAPTURE, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_fill_properties_called);

  alsa_iodev_destroy((struct cras_iodev *)aio);
}

TEST(AlsaIoInit, OpenCapture) {
  struct cras_iodev *iodev;
  struct cras_audio_format *format = NULL;

  iodev = alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                            ALSA_CARD_TYPE_INTERNAL, 0,
                            fake_mixer, NULL,
                            CRAS_STREAM_INPUT);

  cras_iodev_set_format(iodev, format);

  ResetStubData();
  iodev->open_dev(iodev);
  EXPECT_EQ(1, cras_alsa_open_called);
  EXPECT_EQ(1, cras_alsa_mixer_get_minimum_capture_gain_called);
  EXPECT_EQ(1, cras_alsa_mixer_get_maximum_capture_gain_called);
  EXPECT_EQ(1, sys_set_capture_gain_limits_called);
  EXPECT_EQ(1, sys_get_capture_gain_called);
  EXPECT_EQ(1, alsa_mixer_set_capture_dBFS_called);
  EXPECT_EQ(1, sys_get_capture_mute_called);
  EXPECT_EQ(1, alsa_mixer_set_capture_mute_called);
  EXPECT_EQ(1, cras_alsa_start_called);

  alsa_iodev_destroy(iodev);
  free(fake_format);
}

TEST(AlsaIoInit, UpdateActiveNode) {
  struct cras_iodev *iodev;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  iodev = alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                            ALSA_CARD_TYPE_INTERNAL, 0,
                            fake_mixer, NULL,
                            CRAS_STREAM_OUTPUT);

  EXPECT_EQ(1, cras_iodev_list_node_selected_called);
  iodev->update_active_node(iodev);
  EXPECT_EQ(2, cras_iodev_list_node_selected_called);

  alsa_iodev_destroy(iodev);
}

TEST(AlsaIoInit, DspNameDefault) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;
  snd_use_case_mgr_t * const fake_ucm = (snd_use_case_mgr_t*)3;

  ResetStubData();
  ucm_get_dsp_name_default_value = "hello";
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, fake_ucm,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->alsa_stream);
  EXPECT_EQ(1, ucm_get_dsp_name_default_called);
  EXPECT_EQ(1, cras_alsa_jack_get_dsp_name_called);
  EXPECT_STREQ("hello", cras_iodev_update_dsp_name);

  alsa_iodev_destroy((struct cras_iodev *)aio);
}

TEST(AlsaIoInit, DspNameJackOverride) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;
  snd_use_case_mgr_t * const fake_ucm = (snd_use_case_mgr_t*)3;
  const struct cras_alsa_jack *jack = (struct cras_alsa_jack*)4;

  ResetStubData();
  ucm_get_dsp_name_default_value = "default_dsp";
  cras_alsa_jack_get_dsp_name_value = "override_dsp";
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, fake_ucm,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->alsa_stream);
  EXPECT_EQ(1, ucm_get_dsp_name_default_called);
  EXPECT_EQ(1, cras_alsa_jack_get_dsp_name_called);
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  EXPECT_STREQ("default_dsp", cras_iodev_update_dsp_name);

  // Add the jack node.
  cras_alsa_jack_list_create_cb(jack, 1, cras_alsa_jack_list_create_cb_data);
  EXPECT_EQ(1, ucm_get_dsp_name_default_called);

  // Mark the jack node as active.
  alsa_iodev_set_active_node(&aio->base, aio->base.nodes->next);
  EXPECT_EQ(2, cras_alsa_jack_get_dsp_name_called);
  EXPECT_EQ(2, cras_iodev_update_dsp_called);
  EXPECT_STREQ("override_dsp", cras_iodev_update_dsp_name);

  // Mark the default node as active.
  alsa_iodev_set_active_node(&aio->base, aio->base.nodes);
  EXPECT_EQ(1, ucm_get_dsp_name_default_called);
  EXPECT_EQ(3, cras_alsa_jack_get_dsp_name_called);
  EXPECT_EQ(3, cras_iodev_update_dsp_called);
  EXPECT_STREQ("default_dsp", cras_iodev_update_dsp_name);

  alsa_iodev_destroy((struct cras_iodev *)aio);
}

TEST(AlsaIoInit, NodeTypeOverride) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;
  snd_use_case_mgr_t * const fake_ucm = (snd_use_case_mgr_t*)3;
  const struct cras_alsa_jack *jack = (struct cras_alsa_jack*)4;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, fake_ucm,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  // Add the jack node.
  cras_alsa_jack_list_create_cb(jack, 1, cras_alsa_jack_list_create_cb_data);
  // Verify that cras_alsa_jack_update_node_type is called when an output device
  // is created.
  EXPECT_EQ(1, cras_alsa_jack_update_node_type_called);

  alsa_iodev_destroy((struct cras_iodev *)aio);
}

TEST(AlsaIoInit, SwapMode) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;
  snd_use_case_mgr_t * const fake_ucm = (snd_use_case_mgr_t*)3;
  struct cras_ionode * const fake_node = (cras_ionode *)4;
  ResetStubData();
  // Stub replies that swap mode does not exist.
  ucm_swap_mode_exists_ret_value = 0;

  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, fake_ucm,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(NULL, aio->base.set_swap_mode_for_node);

  // Stub replies that swap mode exists.
  ucm_swap_mode_exists_ret_value = 1;

  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, fake_ucm,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  // Enable swap mode.
  aio->base.set_swap_mode_for_node((cras_iodev*)aio, fake_node, 1);

  // Verify that ucm_enable_swap_mode is called when callback to enable
  // swap mode is called.
  EXPECT_EQ(1, ucm_enable_swap_mode_called);

  alsa_iodev_destroy((struct cras_iodev *)aio);
}

// Test that system settins aren't touched if no streams active.
TEST(AlsaOutputNode, SystemSettingsWhenInactive) {
  int rc;
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_alsa_mixer_output *outputs[2];

  ResetStubData();
  outputs[0] =
    static_cast<struct cras_alsa_mixer_output *>(calloc(1, sizeof(**outputs)));
  outputs[1] =
    static_cast<struct cras_alsa_mixer_output *>(calloc(1, sizeof(**outputs)));
  fake_curve =
    static_cast<struct cras_volume_curve *>(calloc(1, sizeof(*fake_curve)));
  fake_curve->get_dBFS = fake_get_dBFS;
  outputs[0]->volume_curve = fake_curve;
  outputs[1]->volume_curve = fake_curve;
  cras_alsa_mixer_list_outputs_outputs = outputs;
  cras_alsa_mixer_list_outputs_outputs_length = ARRAY_SIZE(outputs);
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);
  EXPECT_EQ(0, cras_alsa_mixer_list_outputs_device_value);

  ResetStubData();
  rc = alsa_iodev_set_active_node((struct cras_iodev *)aio,
                                  aio->base.nodes->next);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, alsa_mixer_set_mute_called);
  EXPECT_EQ(0, alsa_mixer_set_dBFS_called);
  ASSERT_EQ(2, cras_alsa_mixer_set_output_active_state_called);
  EXPECT_EQ(outputs[0], cras_alsa_mixer_set_output_active_state_outputs[0]);
  EXPECT_EQ(0, cras_alsa_mixer_set_output_active_state_values[0]);
  EXPECT_EQ(outputs[1], cras_alsa_mixer_set_output_active_state_outputs[1]);
  EXPECT_EQ(1, cras_alsa_mixer_set_output_active_state_values[1]);
  EXPECT_EQ(1, cras_iodev_update_dsp_called);
  EXPECT_EQ(2, cras_alsa_jack_enable_ucm_called);

  alsa_iodev_destroy((struct cras_iodev *)aio);
  free(outputs[0]);
  free(outputs[1]);
  free(fake_curve);
  fake_curve = NULL;
}

//  Test handling of different amounts of outputs.
TEST(AlsaOutputNode, TwoOutputs) {
  int rc;
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_alsa_mixer_output *outputs[2];

  ResetStubData();
  outputs[0] =
    static_cast<struct cras_alsa_mixer_output *>(calloc(1, sizeof(**outputs)));
  outputs[1] =
    static_cast<struct cras_alsa_mixer_output *>(calloc(1, sizeof(**outputs)));
  fake_curve =
    static_cast<struct cras_volume_curve *>(calloc(1, sizeof(*fake_curve)));
  fake_curve->get_dBFS = fake_get_dBFS;
  outputs[0]->volume_curve = fake_curve;
  outputs[1]->volume_curve = fake_curve;
  cras_alsa_mixer_list_outputs_outputs = outputs;
  cras_alsa_mixer_list_outputs_outputs_length = ARRAY_SIZE(outputs);
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            ALSA_CARD_TYPE_INTERNAL, 0,
                                            fake_mixer, NULL,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);
  EXPECT_EQ(0, cras_alsa_mixer_list_outputs_device_value);

  aio->handle = (snd_pcm_t *)0x24;

  ResetStubData();
  rc = alsa_iodev_set_active_node((struct cras_iodev *)aio,
                                  aio->base.nodes->next);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(2, alsa_mixer_set_mute_called);
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

  alsa_iodev_destroy((struct cras_iodev *)aio);
  free(outputs[0]);
  free(outputs[1]);
  free(fake_curve);
  fake_curve = NULL;
}

TEST(AlsaInitNode, SetNodeInitialState) {
  struct cras_ionode node;
  struct cras_iodev dev;

  memset(&dev, 0, sizeof(dev));
  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Unknown");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(0, node.plugged_time.tv_sec);
  ASSERT_EQ(CRAS_NODE_TYPE_UNKNOWN, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Speaker");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(1, node.plugged);
  ASSERT_GT(node.plugged_time.tv_sec, 0);
  ASSERT_EQ(CRAS_NODE_TYPE_INTERNAL_SPEAKER, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Internal Mic");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(1, node.plugged);
  ASSERT_GT(node.plugged_time.tv_sec, 0);
  ASSERT_EQ(CRAS_NODE_TYPE_INTERNAL_MIC, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "HDMI");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(0, node.plugged_time.tv_sec);
  ASSERT_EQ(CRAS_NODE_TYPE_HDMI, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "IEC958");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_HDMI, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "HDMI Jack");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_HDMI, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Headphone");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_HEADPHONE, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Headphone Jack");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_HEADPHONE, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Mic");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_MIC, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Mic Jack");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_MIC, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Unknown");
  set_node_initial_state(&node, ALSA_CARD_TYPE_USB);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_USB, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  dev.direction = CRAS_STREAM_INPUT;
  strcpy(node.name, "DAISY-I2S Mic Jack");
  set_node_initial_state(&node, ALSA_CARD_TYPE_INTERNAL);
  ASSERT_EQ(0, node.plugged);
  ASSERT_EQ(CRAS_NODE_TYPE_MIC, node.type);

  memset(&node, 0, sizeof(node));
  node.dev = &dev;
  strcpy(node.name, "Speaker");
  set_node_initial_state(&node, ALSA_CARD_TYPE_USB);
  ASSERT_EQ(1, node.plugged);
  ASSERT_GT(node.plugged_time.tv_sec, 0);
  ASSERT_EQ(CRAS_NODE_TYPE_USB, node.type);
}

//  Test thread add/rm stream, open_alsa, and iodev config.
class AlsaVolumeMuteSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      aio_output_ = (struct alsa_io *)alsa_iodev_create(
          0, test_card_name, 0, test_dev_name,
          ALSA_CARD_TYPE_INTERNAL, 0,
          fake_mixer, NULL,
          CRAS_STREAM_OUTPUT);
      aio_output_->base.direction = CRAS_STREAM_OUTPUT;
      aio_input_ = (struct alsa_io *)alsa_iodev_create(
          0, test_card_name, 0, test_dev_name,
          ALSA_CARD_TYPE_INTERNAL, 0,
          fake_mixer, NULL,
          CRAS_STREAM_INPUT);
      aio_input_->base.direction = CRAS_STREAM_INPUT;
      fmt_.frame_rate = 44100;
      fmt_.num_channels = 2;
      fmt_.format = SND_PCM_FORMAT_S16_LE;
      aio_input_->base.format = &fmt_;
      aio_output_->base.format = &fmt_;
      ResetStubData();
      cras_alsa_get_avail_frames_ret = -1;
      fake_curve =
        static_cast<struct cras_volume_curve *>(calloc(1, sizeof(*fake_curve)));
      fake_curve->get_dBFS = fake_get_dBFS;
    }

    virtual void TearDown() {
      alsa_iodev_destroy((struct cras_iodev *)aio_output_);
      alsa_iodev_destroy((struct cras_iodev *)aio_input_);
      cras_alsa_get_avail_frames_ret = 0;
      free(fake_curve);
      fake_curve = NULL;
    }

  struct alsa_io *aio_output_;
  struct alsa_io *aio_input_;
  struct cras_audio_format fmt_;
};

TEST_F(AlsaVolumeMuteSuite, SetVolumeAndMute) {
  int rc;
  struct cras_audio_format *fmt;
  const size_t fake_system_volume = 55;
  const size_t fake_system_volume_dB = (fake_system_volume - 100) * 100;

  fmt = (struct cras_audio_format *)malloc(sizeof(*fmt));
  memcpy(fmt, &fmt_, sizeof(fmt_));
  aio_output_->base.format = fmt;
  aio_output_->handle = (snd_pcm_t *)0x24;

  aio_output_->num_underruns = 3; //  Something non-zero.
  sys_get_volume_return_value = fake_system_volume;
  rc = aio_output_->base.open_dev(&aio_output_->base);
  ASSERT_EQ(0, rc);
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  EXPECT_EQ(fake_system_volume_dB, alsa_mixer_set_dBFS_value);
  EXPECT_EQ(1, alsa_mixer_set_mute_called);
  EXPECT_EQ(0, alsa_mixer_set_mute_value);

  alsa_mixer_set_mute_called = 0;
  alsa_mixer_set_mute_value = 0;
  alsa_mixer_set_dBFS_called = 0;
  alsa_mixer_set_dBFS_value = 0;
  sys_get_volume_return_value = 50;
  sys_get_volume_called = 0;
  aio_output_->base.set_volume(&aio_output_->base);
  EXPECT_EQ(1, sys_get_volume_called);
  EXPECT_EQ(1, alsa_mixer_set_mute_called);
  EXPECT_EQ(0, alsa_mixer_set_mute_value);
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  EXPECT_EQ(-5000, alsa_mixer_set_dBFS_value);
  EXPECT_EQ(NULL, alsa_mixer_set_dBFS_output);

  alsa_mixer_set_mute_called = 0;
  alsa_mixer_set_mute_value = 0;
  alsa_mixer_set_dBFS_called = 0;
  alsa_mixer_set_dBFS_value = 0;
  sys_get_volume_return_value = 0;
  sys_get_volume_called = 0;
  aio_output_->base.set_volume(&aio_output_->base);
  EXPECT_EQ(1, sys_get_volume_called);
  EXPECT_EQ(1, alsa_mixer_set_mute_called);
  EXPECT_EQ(1, alsa_mixer_set_mute_value);
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  EXPECT_EQ(-10000, alsa_mixer_set_dBFS_value);

  sys_get_volume_return_value = 80;
  aio_output_->base.active_node->volume = 90;
  aio_output_->base.set_volume(&aio_output_->base);
  EXPECT_EQ(-3000, alsa_mixer_set_dBFS_value);

  // close the dev.
  rc = aio_output_->base.close_dev(&aio_output_->base);
  EXPECT_EQ(0, rc);
  EXPECT_EQ((void *)NULL, aio_output_->handle);

  free(fmt);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

//  Stubs

extern "C" {

//  From iodev.
int cras_iodev_list_add_output(struct cras_iodev *output)
{
  return 0;
}
int cras_iodev_list_rm_output(struct cras_iodev *dev)
{
  return 0;
}

int cras_iodev_list_add_input(struct cras_iodev *input)
{
  return 0;
}
int cras_iodev_list_rm_input(struct cras_iodev *dev)
{
  return 0;
}

}

//  From alsa helper.
int cras_alsa_set_channel_map(snd_pcm_t *handle,
			      struct cras_audio_format *fmt)
{
  return 0;
}
int cras_alsa_get_channel_map(snd_pcm_t *handle,
			      struct cras_audio_format *fmt)
{
  return 0;
}
int cras_alsa_pcm_open(snd_pcm_t **handle, const char *dev,
		       snd_pcm_stream_t stream)
{
  *handle = (snd_pcm_t *)0x24;
  cras_alsa_open_called++;
  return 0;
}
int cras_alsa_pcm_close(snd_pcm_t *handle)
{
  return 0;
}
int cras_alsa_pcm_start(snd_pcm_t *handle)
{
  cras_alsa_start_called++;
  return 0;
}
int cras_alsa_pcm_drain(snd_pcm_t *handle)
{
  return 0;
}
int cras_alsa_fill_properties(const char *dev,
			      snd_pcm_stream_t stream,
			      size_t **rates,
			      size_t **channel_counts,
			      snd_pcm_format_t **formats)
{
  *rates = (size_t *)malloc(sizeof(**rates) * 3);
  (*rates)[0] = 44100;
  (*rates)[1] = 48000;
  (*rates)[2] = 0;
  *channel_counts = (size_t *)malloc(sizeof(**channel_counts) * 2);
  (*channel_counts)[0] = 2;
  (*channel_counts)[1] = 0;
  *formats = (snd_pcm_format_t *)malloc(sizeof(**formats) * 2);
  (*formats)[0] = SND_PCM_FORMAT_S16_LE;
  (*formats)[1] = (snd_pcm_format_t)0;

  cras_alsa_fill_properties_called++;
  return 0;
}
int cras_alsa_set_hwparams(snd_pcm_t *handle, struct cras_audio_format *format,
			   snd_pcm_uframes_t *buffer_size)
{
  return 0;
}
int cras_alsa_set_swparams(snd_pcm_t *handle)
{
  return 0;
}
int cras_alsa_get_avail_frames(snd_pcm_t *handle, snd_pcm_uframes_t buf_size,
			       snd_pcm_uframes_t *used)
{
  *used = cras_alsa_get_avail_frames_avail;
  return cras_alsa_get_avail_frames_ret;
}
int cras_alsa_get_delay_frames(snd_pcm_t *handle, snd_pcm_uframes_t buf_size,
			       snd_pcm_sframes_t *delay)
{
  *delay = 0;
  return 0;
}
int cras_alsa_mmap_begin(snd_pcm_t *handle, unsigned int format_bytes,
			 uint8_t **dst, snd_pcm_uframes_t *offset,
			 snd_pcm_uframes_t *frames, unsigned int *underruns)
{
  *dst = cras_alsa_mmap_begin_buffer;
  *frames = cras_alsa_mmap_begin_frames;
  return 0;
}
int cras_alsa_mmap_commit(snd_pcm_t *handle, snd_pcm_uframes_t offset,
			  snd_pcm_uframes_t frames, unsigned int *underruns)
{
  return 0;
}
int cras_alsa_attempt_resume(snd_pcm_t *handle)
{
  return 0;
}

//  ALSA stubs.
int snd_pcm_format_physical_width(snd_pcm_format_t format)
{
  return 16;
}

snd_pcm_state_t snd_pcm_state(snd_pcm_t *handle)
{
  return SND_PCM_STATE_RUNNING;
}

const char *snd_strerror(int errnum)
{
  return "Alsa Error in UT";
}

const char *cras_alsa_mixer_get_output_name(
		const struct cras_alsa_mixer_output *output)
{
  return "";
}

//  From system_state.
size_t cras_system_get_volume()
{
  sys_get_volume_called++;
  return sys_get_volume_return_value;
}

long cras_system_get_capture_gain()
{
  sys_get_capture_gain_called++;
  return sys_get_capture_gain_return_value;
}

int cras_system_get_mute()
{
  sys_get_mute_called++;
  return sys_get_mute_return_value;
}

int cras_system_get_capture_mute()
{
  sys_get_capture_mute_called++;
  return sys_get_capture_mute_return_value;
}

void cras_system_set_volume_limits(long min, long max)
{
  sys_set_volume_limits_called++;
}

void cras_system_set_capture_gain_limits(long min, long max)
{
  sys_set_capture_gain_limits_called++;
}

//  From cras_alsa_mixer.
void cras_alsa_mixer_set_dBFS(struct cras_alsa_mixer *m,
			      long dB_level,
			      struct cras_alsa_mixer_output *output)
{
  alsa_mixer_set_dBFS_called++;
  alsa_mixer_set_dBFS_value = dB_level;
  alsa_mixer_set_dBFS_output = output;
}

void cras_alsa_mixer_set_mute(struct cras_alsa_mixer *cras_mixer,
			      int muted,
			      struct cras_alsa_mixer_output *mixer_output)
{
  alsa_mixer_set_mute_called++;
  alsa_mixer_set_mute_value = muted;
  alsa_mixer_set_mute_output = mixer_output;
}

void cras_alsa_mixer_set_capture_dBFS(struct cras_alsa_mixer *m, long dB_level,
		                      struct mixer_volume_control *mixer_input)
{
  alsa_mixer_set_capture_dBFS_called++;
  alsa_mixer_set_capture_dBFS_value = dB_level;
  alsa_mixer_set_capture_dBFS_input = mixer_input;
}

void cras_alsa_mixer_set_capture_mute(struct cras_alsa_mixer *m, int mute)
{
  alsa_mixer_set_capture_mute_called++;
  alsa_mixer_set_capture_mute_value = mute;
}

void cras_alsa_mixer_list_outputs(struct cras_alsa_mixer *cras_mixer,
				  size_t device_index,
				  cras_alsa_mixer_output_callback cb,
				  void *callback_arg)
{
  cras_alsa_mixer_list_outputs_called++;
  cras_alsa_mixer_list_outputs_device_value = device_index;
  for (size_t i = 0; i < cras_alsa_mixer_list_outputs_outputs_length; i++) {
    cb(cras_alsa_mixer_list_outputs_outputs[i], callback_arg);
  }
}

struct cras_volume_curve *cras_alsa_mixer_create_volume_curve_for_name(
		const struct cras_alsa_mixer *cmix,
		const char *name)
{
	return NULL;
}

int cras_alsa_mixer_set_output_active_state(
		struct cras_alsa_mixer_output *output,
		int active)
{
  cras_alsa_mixer_set_output_active_state_called++;
  cras_alsa_mixer_set_output_active_state_outputs.push_back(output);
  cras_alsa_mixer_set_output_active_state_values.push_back(active);
  return 0;
}

const struct cras_volume_curve *cras_alsa_mixer_default_volume_curve(
		const struct cras_alsa_mixer *cras_mixer)
{
  cras_alsa_mixer_default_volume_curve_called++;
  return fake_curve;
}

void cras_volume_curve_destroy(struct cras_volume_curve *curve)
{
}

long cras_alsa_mixer_get_minimum_capture_gain(struct cras_alsa_mixer *cmix,
		struct mixer_volume_control *mixer_input)
{
	cras_alsa_mixer_get_minimum_capture_gain_called++;
	cras_alsa_mixer_get_minimum_capture_gain_mixer_input = mixer_input;
	return 0;
}

long cras_alsa_mixer_get_maximum_capture_gain(struct cras_alsa_mixer *cmix,
		struct mixer_volume_control *mixer_input)
{
	cras_alsa_mixer_get_maximum_capture_gain_called++;
	cras_alsa_mixer_get_maximum_capture_gain_mixer_input = mixer_input;
	return 0;
}

// From cras_alsa_jack
struct cras_alsa_jack_list *cras_alsa_jack_list_create(
		unsigned int card_index,
		const char *card_name,
		unsigned int device_index,
		int check_gpio_jack,
		struct cras_alsa_mixer *mixer,
		snd_use_case_mgr_t *ucm,
		enum CRAS_STREAM_DIRECTION direction,
		jack_state_change_callback *cb,
		void *cb_data)
{
  cras_alsa_jack_list_create_called++;
  cras_alsa_jack_list_create_cb = cb;
  cras_alsa_jack_list_create_cb_data = cb_data;
  return (struct cras_alsa_jack_list *)0xfee;
}

void cras_alsa_jack_list_destroy(struct cras_alsa_jack_list *jack_list)
{
  cras_alsa_jack_list_destroy_called++;
}

void cras_alsa_jack_list_report(const struct cras_alsa_jack_list *jack_list)
{
}

void cras_alsa_jack_enable_ucm(const struct cras_alsa_jack *jack, int enable) {
  cras_alsa_jack_enable_ucm_called++;
}

const char *cras_alsa_jack_get_name(const struct cras_alsa_jack *jack)
{
  return "";
}

const char *cras_alsa_jack_get_dsp_name(const struct cras_alsa_jack *jack)
{
  cras_alsa_jack_get_dsp_name_called++;
  return jack ? cras_alsa_jack_get_dsp_name_value : NULL;
}

const char *ucm_get_dsp_name_default(snd_use_case_mgr_t *mgr, int direction)
{
  ucm_get_dsp_name_default_called++;
  if (ucm_get_dsp_name_default_value)
    return strdup(ucm_get_dsp_name_default_value);
  else
    return NULL;
}

struct cras_alsa_mixer_output *cras_alsa_jack_get_mixer_output(
    const struct cras_alsa_jack *jack)
{
  return NULL;
}

struct mixer_volume_control *cras_alsa_jack_get_mixer_input(
		const struct cras_alsa_jack *jack)
{
  return NULL;
}

int ucm_set_enabled(snd_use_case_mgr_t *mgr, const char *dev, int enabled) {
  return 0;
}

char *ucm_get_flag(snd_use_case_mgr_t *mgr, const char *flag_name) {
  return NULL;
}

int ucm_swap_mode_exists(snd_use_case_mgr_t *mgr)
{
  return ucm_swap_mode_exists_ret_value;
}

int ucm_enable_swap_mode(snd_use_case_mgr_t *mgr, const char *node_name,
                         int enable)
{
  ucm_enable_swap_mode_called++;
  return ucm_enable_swap_mode_ret_value;
}
void cras_iodev_free_format(struct cras_iodev *iodev)
{
}

int cras_iodev_set_format(struct cras_iodev *iodev,
			  struct cras_audio_format *fmt)
{
  fake_format = (struct cras_audio_format *)calloc(1, sizeof(*fake_format));
  iodev->format = fake_format;
  return 0;
}

audio_thread* audio_thread_create(cras_iodev* iodev) {
  return reinterpret_cast<audio_thread*>(0x323);
}

void audio_thread_destroy(audio_thread* thread) {
}

void cras_iodev_update_dsp(struct cras_iodev *iodev)
{
  cras_iodev_update_dsp_called++;
  cras_iodev_update_dsp_name = iodev->dsp_name;
}

int cras_iodev_set_node_attr(struct cras_ionode *ionode,
			     enum ionode_attr attr, int value)
{
  cras_iodev_set_node_attr_called++;
  cras_iodev_set_node_attr_attr = attr;
  cras_iodev_set_node_attr_value = value;
  return 0;
}

int cras_iodev_list_node_selected(struct cras_ionode *node)
{
  cras_iodev_list_node_selected_called++;
  return 1;
}

void cras_iodev_add_node(struct cras_iodev *iodev, struct cras_ionode *node)
{
  DL_APPEND(iodev->nodes, node);
}

void cras_iodev_rm_node(struct cras_iodev *iodev, struct cras_ionode *node)
{
  DL_DELETE(iodev->nodes, node);
}

void cras_iodev_set_active_node(struct cras_iodev *iodev,
                                struct cras_ionode *node)
{
  iodev->active_node = node;
}

void cras_iodev_free_resources(struct cras_iodev *iodev)
{
  cras_iodev_free_resources_called++;
}

int cras_alsa_jack_exists(unsigned int card_index, const char *jack_name)
{
  cras_alsa_jack_exists_called++;
  if (cras_alsa_jack_exists_match)
    return strcmp(cras_alsa_jack_exists_match, jack_name) == 0;
  return 0;
}

void cras_alsa_jack_update_monitor_name(const struct cras_alsa_jack *jack,
					char *name_buf,
					unsigned int buf_size)
{
}

void cras_alsa_jack_update_node_type(const struct cras_alsa_jack *jack,
				     enum CRAS_NODE_TYPE *type)
{
  cras_alsa_jack_update_node_type_called++;
}

void cras_iodev_init_audio_area(struct cras_iodev *iodev,
                                int num_channels) {
}

void cras_iodev_free_audio_area(struct cras_iodev *iodev) {
}

void cras_audio_area_config_buf_pointers(struct cras_audio_area *area,
					 const struct cras_audio_format *fmt,
					 uint8_t *base_buffer)
{
}

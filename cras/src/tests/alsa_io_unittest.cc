// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <sys/select.h>
#include <gtest/gtest.h>

extern "C" {
//  Override select so it can be stubbed.
static int select_return_value;
static struct timeval select_timeval;
static int select_max_fd;
static fd_set select_in_fds;
static fd_set select_out_fds;
int ut_select(int nfds,
              fd_set *readfds,
              fd_set *writefds,
              fd_set *exceptfds,
              struct timeval *timeout) {
  select_max_fd = nfds;
  select_timeval.tv_sec = timeout->tv_sec;
  select_timeval.tv_usec = timeout->tv_usec;
  select_in_fds = *readfds;
  *readfds = select_out_fds;
  return select_return_value;
}
#define select ut_select

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
static size_t cras_iodev_post_message_to_playback_thread_called;
static size_t cras_iodev_init_called;
static size_t cras_iodev_deinit_called;
static size_t sys_set_volume_limits_called;
static size_t sys_set_capture_gain_limits_called;
static size_t cras_alsa_mixer_get_minimum_capture_gain_called;
static size_t cras_alsa_mixer_get_maximum_capture_gain_called;
static size_t cras_alsa_jack_list_create_called;
static size_t cras_alsa_jack_list_destroy_called;
static jack_state_change_callback *cras_alsa_jack_list_create_cb;
static void *cras_alsa_jack_list_create_cb_data;
static size_t cras_iodev_move_stream_type_top_prio_called;
static char test_card_name[] = "TestCard";
static char test_dev_name[] = "TestDev";
static size_t cras_iodev_plug_event_called;
static int cras_iodev_plug_event_value;
static unsigned cras_alsa_jack_enable_ucm_called;

void ResetStubData() {
  cras_alsa_open_called = 0;
  cras_iodev_append_stream_ret = 0;
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = 0;
  cras_alsa_start_called = 0;
  select_return_value = 0;
  select_max_fd = -1;
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
  cras_iodev_post_message_to_playback_thread_called = 0;
  cras_iodev_init_called = 0;
  cras_iodev_deinit_called = 0;
  sys_set_volume_limits_called = 0;
  sys_set_capture_gain_limits_called = 0;
  cras_alsa_mixer_get_minimum_capture_gain_called = 0;
  cras_alsa_mixer_get_maximum_capture_gain_called = 0;
  cras_alsa_jack_list_create_called = 0;
  cras_alsa_jack_list_destroy_called = 0;
  cras_iodev_move_stream_type_top_prio_called = 0;
  cras_iodev_plug_event_called = 0;
  cras_alsa_jack_enable_ucm_called = 0;
}

static long fake_get_dBFS(const cras_volume_curve *curve, size_t volume)
{
  return (volume - 100) * 100;
}

namespace {

TEST(AlsaIoInit, InitializePlayback) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            fake_mixer, NULL, 7,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_fill_properties_called);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);
  EXPECT_EQ(0, cras_alsa_mixer_list_outputs_device_value);
  EXPECT_EQ(0, strncmp(test_card_name,
                       aio->base.info.name,
		       strlen(test_card_name)));
  EXPECT_EQ(7, aio->base.info.priority);

  alsa_iodev_destroy((struct cras_iodev *)aio);
}

TEST(AlsaIoInit, RouteBasedOnJackCallback) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            fake_mixer, NULL, 0,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_fill_properties_called);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);
  EXPECT_EQ(0, cras_alsa_mixer_list_outputs_device_value);
  EXPECT_EQ(1, cras_alsa_jack_list_create_called);

  fake_curve =
    static_cast<struct cras_volume_curve *>(calloc(1, sizeof(*fake_curve)));
  fake_curve->get_dBFS = fake_get_dBFS;

  cras_alsa_jack_list_create_cb(NULL, 1, cras_alsa_jack_list_create_cb_data);
  EXPECT_EQ(1, cras_iodev_move_stream_type_top_prio_called);
  EXPECT_EQ(1, cras_iodev_plug_event_called);
  EXPECT_EQ(1, cras_iodev_plug_event_value);
  EXPECT_EQ(1, cras_alsa_jack_enable_ucm_called);
  cras_alsa_jack_list_create_cb(NULL, 0, cras_alsa_jack_list_create_cb_data);
  EXPECT_EQ(2, cras_iodev_move_stream_type_top_prio_called);
  EXPECT_EQ(2, cras_iodev_plug_event_called);
  EXPECT_EQ(0, cras_iodev_plug_event_value);

  alsa_iodev_destroy((struct cras_iodev *)aio);
  EXPECT_EQ(1, cras_alsa_jack_list_destroy_called);
  free(fake_curve);
}

TEST(AlsaIoInit, RouteBasedOnInputJackCallback) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            fake_mixer, NULL, 0,
                                            CRAS_STREAM_INPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_CAPTURE, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_fill_properties_called);
  EXPECT_EQ(1, cras_alsa_jack_list_create_called);

  fake_curve =
    static_cast<struct cras_volume_curve *>(calloc(1, sizeof(*fake_curve)));
  fake_curve->get_dBFS = fake_get_dBFS;

  cras_alsa_jack_list_create_cb(NULL, 1, cras_alsa_jack_list_create_cb_data);
  EXPECT_EQ(1, cras_iodev_move_stream_type_top_prio_called);
  EXPECT_EQ(1, cras_iodev_plug_event_called);
  EXPECT_EQ(1, cras_iodev_plug_event_value);
  EXPECT_EQ(1, cras_alsa_jack_enable_ucm_called);
  cras_alsa_jack_list_create_cb(NULL, 0, cras_alsa_jack_list_create_cb_data);
  EXPECT_EQ(2, cras_iodev_move_stream_type_top_prio_called);
  EXPECT_EQ(2, cras_iodev_plug_event_called);
  EXPECT_EQ(0, cras_iodev_plug_event_value);

  alsa_iodev_destroy((struct cras_iodev *)aio);
  EXPECT_EQ(1, cras_alsa_jack_list_destroy_called);
  free(fake_curve);
}

TEST(AlsaIoInit, InitializeCapture) {
  struct alsa_io *aio;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0, test_dev_name,
                                            fake_mixer, NULL, 0,
                                            CRAS_STREAM_INPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_CAPTURE, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_fill_properties_called);

  alsa_iodev_destroy((struct cras_iodev *)aio);
}

// Test that system settins aren't touched if no streams active.
TEST(AlsaOutputNode, SystemSettingsWhenInactive) {
  int rc;
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_alsa_mixer_output *outputs[2];
  struct cras_alsa_mixer_output *fake_output =
      reinterpret_cast<struct cras_alsa_mixer_output *>(7);

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
                                            fake_mixer, NULL, 0,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);
  EXPECT_EQ(0, cras_alsa_mixer_list_outputs_device_value);

  rc = alsa_iodev_set_active_output((struct cras_iodev *)aio, fake_output);
  EXPECT_EQ(-EINVAL, rc);
  ResetStubData();

  rc = alsa_iodev_set_active_output((struct cras_iodev *)aio, outputs[0]);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, alsa_mixer_set_mute_called);
  EXPECT_EQ(0, alsa_mixer_set_dBFS_called);
  ASSERT_EQ(2, cras_alsa_mixer_set_output_active_state_called);
  EXPECT_EQ(outputs[0], cras_alsa_mixer_set_output_active_state_outputs[0]);
  EXPECT_EQ(1, cras_alsa_mixer_set_output_active_state_values[0]);
  EXPECT_EQ(outputs[1], cras_alsa_mixer_set_output_active_state_outputs[1]);
  EXPECT_EQ(0, cras_alsa_mixer_set_output_active_state_values[1]);

  alsa_iodev_destroy((struct cras_iodev *)aio);
  free(outputs[0]);
  free(outputs[1]);
  free(fake_curve);
}

//  Test handling of different amounts of outputs.
TEST(AlsaOutputNode, TwoOutputs) {
  int rc;
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;
  struct cras_alsa_mixer_output *outputs[2];
  struct cras_alsa_mixer_output *fake_output =
      reinterpret_cast<struct cras_alsa_mixer_output *>(7);

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
                                            fake_mixer, NULL, 0,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->alsa_stream);
  EXPECT_EQ(1, cras_alsa_mixer_list_outputs_called);
  EXPECT_EQ(0, cras_alsa_mixer_list_outputs_device_value);

  aio->handle = (snd_pcm_t *)0x24;

  rc = alsa_iodev_set_active_output((struct cras_iodev *)aio, fake_output);
  EXPECT_EQ(-EINVAL, rc);
  ResetStubData();
  rc = alsa_iodev_set_active_output((struct cras_iodev *)aio, outputs[0]);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(2, alsa_mixer_set_mute_called);
  EXPECT_EQ(outputs[0], alsa_mixer_set_mute_output);
  EXPECT_EQ(1, alsa_mixer_set_dBFS_called);
  EXPECT_EQ(outputs[0], alsa_mixer_set_dBFS_output);
  ASSERT_EQ(2, cras_alsa_mixer_set_output_active_state_called);
  EXPECT_EQ(outputs[0], cras_alsa_mixer_set_output_active_state_outputs[0]);
  EXPECT_EQ(1, cras_alsa_mixer_set_output_active_state_values[0]);
  EXPECT_EQ(outputs[1], cras_alsa_mixer_set_output_active_state_outputs[1]);
  EXPECT_EQ(0, cras_alsa_mixer_set_output_active_state_values[1]);

  alsa_iodev_destroy((struct cras_iodev *)aio);
  free(outputs[0]);
  free(outputs[1]);
  free(fake_curve);
}

//  Test thread add/rm stream, open_alsa, and iodev config.
class AlsaVolumeMuteSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      aio_output_ = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0,
          test_dev_name, fake_mixer, NULL, 0, CRAS_STREAM_OUTPUT);
      aio_output_->base.direction = CRAS_STREAM_OUTPUT;
      aio_input_ = (struct alsa_io *)alsa_iodev_create(0, test_card_name, 0,
          test_dev_name, fake_mixer, NULL, 0, CRAS_STREAM_INPUT);
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

  // close the dev.
  rc = aio_output_->base.close_dev(&aio_output_->base);
  EXPECT_EQ(0, rc);
  EXPECT_EQ((void *)NULL, aio_output_->handle);
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
int cras_iodev_move_stream_type_top_prio(enum CRAS_STREAM_TYPE type,
                                         enum CRAS_STREAM_DIRECTION direction)
{
  cras_iodev_move_stream_type_top_prio_called++;
  return 0;
}

int cras_iodev_init(struct cras_iodev *iodev,
		    void *(*thread_function)(void *arg),
		    void *thread_data)
{
  cras_iodev_init_called++;
  return 0;
}
void cras_iodev_deinit(struct cras_iodev *dev)
{
  cras_iodev_deinit_called++;
}
int cras_iodev_post_message_to_playback_thread(struct cras_iodev *iodev,
					       struct cras_iodev_msg *msg)
{
  cras_iodev_post_message_to_playback_thread_called++;
  return 0;
}
}
void cras_iodev_plug_event(struct cras_iodev *iodev, int plugged)
{
	cras_iodev_plug_event_called++;
	cras_iodev_plug_event_value = plugged;
}

//  From alsa helper.
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
			      size_t **channel_counts)
{
  *rates = (size_t *)malloc(sizeof(**rates) * 3);
  (*rates)[0] = 44100;
  (*rates)[1] = 48000;
  (*rates)[2] = 0;
  *channel_counts = (size_t *)malloc(sizeof(**channel_counts) * 2);
  (*channel_counts)[0] = 2;
  (*channel_counts)[1] = 0;

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

void cras_iodev_free_format(struct cras_iodev *iodev)
{
}

audio_thread* audio_thread_create(cras_iodev* iodev) {
  return reinterpret_cast<audio_thread*>(0x323);
}

void audio_thread_destroy(audio_thread* thread) {
}

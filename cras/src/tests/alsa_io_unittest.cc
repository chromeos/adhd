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

#include "cras_shm.h"
#include "cras_system_settings.h"
#include "cras_types.h"

//  Include C file to test static functions.
#include "cras_alsa_io.c"
}

//  Data for simulating functions stubbed below.
static struct timespec clock_gettime_retspec;
static int cras_alsa_open_called;
static int cras_iodev_append_stream_ret;
static int cras_alsa_get_avail_frames_ret;
static int cras_alsa_get_avail_frames_avail;
static int cras_alsa_start_called;
static int cras_rstream_audio_ready_count;
static uint8_t *cras_alsa_mmap_begin_buffer;
static size_t cras_alsa_mmap_begin_frames;
static size_t cras_mix_add_stream_count;
static int cras_mix_add_stream_dont_fill_next;
static size_t cras_rstream_request_audio_called;
static size_t cras_alsa_fill_properties_called;
static struct cras_alsa_mixer *mixer_destroy_value;
static struct cras_alsa_mixer *mixer_create_return_value;
static size_t alsa_mixer_set_volume_called;
static int alsa_mixer_set_volume_value;
static size_t mixer_destroy_called;
static cras_system_volume_changed_cb sys_register_volume_cb_value;
static void * sys_register_volume_cb_arg;
static size_t sys_register_volume_cb_called;
static size_t sys_get_volume_called;
static size_t sys_get_volume_return_value;
static size_t alsa_mixer_set_mute_called;
static int alsa_mixer_set_mute_value;
static cras_system_mute_changed_cb sys_register_mute_cb_value;
static void * sys_register_mute_cb_arg;
static size_t sys_register_mute_cb_called;
static size_t sys_get_mute_called;
static int sys_get_mute_return_value;
static struct cras_alsa_mixer *fake_mixer = (struct cras_alsa_mixer *)1;

void ResetStubData() {
  cras_alsa_open_called = 0;
  cras_iodev_append_stream_ret = 0;
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = 0;
  cras_alsa_start_called = 0;
  select_return_value = 0;
  cras_rstream_request_audio_called = 0;
  select_max_fd = -1;
  cras_mix_add_stream_dont_fill_next = 0;
  cras_alsa_fill_properties_called = 0;
  mixer_destroy_called = 0;
  sys_register_volume_cb_called = 0;
  sys_get_volume_called = 0;
  alsa_mixer_set_volume_called = 0;
  sys_register_mute_cb_called = 0;
  sys_get_mute_called = 0;
  alsa_mixer_set_mute_called = 0;
}

namespace {

TEST(AlsaIoInit, InitializePlayback) {
  struct alsa_io *aio;
  struct cras_alsa_mixer * const fake_mixer = (struct cras_alsa_mixer*)2;

  ResetStubData();
  mixer_create_return_value = fake_mixer;
  aio = (struct alsa_io *)alsa_iodev_create("hw:0,0",
                                            fake_mixer,
                                            CRAS_STREAM_OUTPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_PLAYBACK, aio->alsa_stream);
  EXPECT_EQ((void *)possibly_fill_audio, (void *)aio->alsa_cb);
  EXPECT_EQ(1, cras_alsa_fill_properties_called);

  alsa_iodev_destroy((struct cras_iodev *)aio);
  EXPECT_EQ(1, mixer_destroy_called);
  EXPECT_EQ(fake_mixer, mixer_destroy_value);
}

TEST(AlsaIoInit, InitializeCapture) {
  struct alsa_io *aio;

  ResetStubData();
  aio = (struct alsa_io *)alsa_iodev_create("hw:0,0",
                                            fake_mixer,
                                            CRAS_STREAM_INPUT);
  ASSERT_NE(aio, (void *)NULL);
  EXPECT_EQ(SND_PCM_STREAM_CAPTURE, aio->alsa_stream);
  EXPECT_EQ((void *)possibly_read_audio, (void *)aio->alsa_cb);
  EXPECT_EQ(1, cras_alsa_fill_properties_called);

  alsa_iodev_destroy((struct cras_iodev *)aio);
}

//  Test set_playback_timestamp.
TEST(AlsaTimeStampTestSuite, SetPlaybackTimeStampSimple) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 0;
  set_playback_timestamp(48000, 24000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 499900000);
  EXPECT_LE(ts.tv_nsec, 500100000);
}

TEST(AlsaTimeStampTestSuite, SetPlaybackTimeStampWrap) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  set_playback_timestamp(48000, 24000, &ts);
  EXPECT_EQ(2, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(AlsaTimeStampTestSuite, SetPlaybackTimeStampWrapTwice) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  set_playback_timestamp(48000, 72000, &ts);
  EXPECT_EQ(3, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

//  Test set_capture_timestamp.
TEST(AlsaTimeStampTestSuite, SetCaptureTimeStampSimple) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  set_capture_timestamp(48000, 24000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(AlsaTimeStampTestSuite, SetCaptureTimeStampWrap) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 0;
  set_capture_timestamp(48000, 24000, &ts);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 499900000);
  EXPECT_LE(ts.tv_nsec, 500100000);
}

TEST(AlsaTimeStampTestSuite, SetCaptureTimeStampWrapPartial) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 2;
  clock_gettime_retspec.tv_nsec = 750000000;
  set_capture_timestamp(48000, 72000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

//  Test fill_time_from_frames
TEST(AlsaTimeStampTestSuite, FillTimeFromFramesNormal) {
  struct timespec ts;

  fill_time_from_frames(24000, 12000, 48000, &ts);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(AlsaTimeStampTestSuite, FillTimeFromFramesLong) {
  struct timespec ts;

  fill_time_from_frames(120000, 12000, 48000, &ts);
  EXPECT_EQ(2, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(AlsaTimeStampTestSuite, FillTimeFromFramesShort) {
  struct timespec ts;

  fill_time_from_frames(12000, 12000, 48000, &ts);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_EQ(0, ts.tv_nsec);
}

//  Test thread add/rm stream, open_alsa, and iodev config.
class AlsaAddStreamSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      aio_output_ = (struct alsa_io *)alsa_iodev_create("hw:0,0", fake_mixer,
          CRAS_STREAM_OUTPUT);
      aio_input_ = (struct alsa_io *)alsa_iodev_create("hw:0,0", fake_mixer,
          CRAS_STREAM_INPUT);
      fmt_.frame_rate = 44100;
      fmt_.num_channels = 2;
      fmt_.format = SND_PCM_FORMAT_S16_LE;
      aio_input_->base.format = &fmt_;
      aio_output_->base.format = &fmt_;
      ResetStubData();
      cras_alsa_get_avail_frames_ret = -1;
    }

    virtual void TearDown() {
      alsa_iodev_destroy((struct cras_iodev *)aio_output_);
      alsa_iodev_destroy((struct cras_iodev *)aio_input_);
      cras_alsa_get_avail_frames_ret = 0;
    }

  struct alsa_io *aio_output_;
  struct alsa_io *aio_input_;
  struct cras_audio_format fmt_;
};

TEST_F(AlsaAddStreamSuite, SimpleAddOutputStream) {
  int rc;
  struct cras_rstream *new_stream;
  struct cras_audio_format *fmt;
  const size_t fake_system_volume = 55;
  const size_t fake_system_volume_dB = (fake_system_volume - 100) * 100;

  fmt = (struct cras_audio_format *)malloc(sizeof(*fmt));
  memcpy(fmt, &fmt_, sizeof(fmt_));
  aio_output_->base.format = fmt;
  new_stream = (struct cras_rstream *)calloc(1, sizeof(*new_stream));
  new_stream->fd = 55;
  new_stream->buffer_frames = 65;
  new_stream->cb_threshold = 80;
  memcpy(&new_stream->format, fmt, sizeof(*fmt));
  aio_output_->num_underruns = 3; //  Something non-zero.
  sys_get_volume_return_value = fake_system_volume;
  rc = thread_add_stream(aio_output_, new_stream);
  ASSERT_EQ(0, rc);
  EXPECT_EQ(55, aio_output_->base.streams->stream->fd);
  EXPECT_EQ(1, cras_alsa_open_called);
  //  Test that config_alsa_iodev_params was run.
  EXPECT_EQ(65, aio_output_->used_size);
  EXPECT_EQ(80, aio_output_->cb_threshold);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, aio_output_->base.format->format);
  //  open_alsa should configure the following.
  EXPECT_EQ(0, aio_output_->num_underruns);
  EXPECT_EQ(0, cras_alsa_start_called); //  Shouldn't start playback.
  EXPECT_NE((void *)NULL, aio_output_->handle);
  EXPECT_EQ(1, alsa_mixer_set_volume_called);
  EXPECT_EQ(fake_system_volume_dB, alsa_mixer_set_volume_value);
  EXPECT_EQ(1, alsa_mixer_set_mute_called);
  EXPECT_EQ(0, alsa_mixer_set_mute_value);

  //  remove the stream.
  rc = thread_remove_stream(aio_output_, new_stream);
  EXPECT_EQ(0, rc);
  EXPECT_EQ((void *)NULL, aio_output_->handle);

  free(new_stream);
}

TEST_F(AlsaAddStreamSuite, AddRmTwoOutputStreams) {
  int rc;
  struct cras_rstream *new_stream, *second_stream;
  struct cras_audio_format *fmt;

  fmt = (struct cras_audio_format *)malloc(sizeof(*fmt));
  memcpy(fmt, &fmt_, sizeof(fmt_));
  aio_output_->base.format = fmt;
  new_stream = (struct cras_rstream *)calloc(1, sizeof(*new_stream));
  new_stream->fd = 55;
  new_stream->buffer_frames = 65;
  new_stream->cb_threshold = 80;
  memcpy(&new_stream->format, fmt, sizeof(*fmt));
  rc = thread_add_stream(aio_output_, new_stream);
  ASSERT_EQ(0, rc);

  //  Second stream has lower latency(config_alsa_iodev_params should re-config)
  second_stream = (struct cras_rstream *)calloc(1, sizeof(*second_stream));
  second_stream->fd = 56;
  second_stream->buffer_frames = 25;
  second_stream->cb_threshold = 12;
  memcpy(&second_stream->format, fmt, sizeof(*fmt));
  rc = thread_add_stream(aio_output_, second_stream);
  ASSERT_EQ(0, rc);
  EXPECT_EQ(25, aio_output_->used_size);
  EXPECT_EQ(12, aio_output_->cb_threshold);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, aio_output_->base.format->format);

  //  remove the stream.
  rc = thread_remove_stream(aio_output_, second_stream);
  EXPECT_EQ(0, rc);
  EXPECT_NE((void *)NULL, aio_output_->handle);
  //  Params should be back to first stream.
  EXPECT_EQ(65, aio_output_->used_size);
  EXPECT_EQ(80, aio_output_->cb_threshold);
  rc = thread_remove_stream(aio_output_, new_stream);
  EXPECT_EQ(0, rc);
  EXPECT_EQ((void *)NULL, aio_output_->handle);

  free(new_stream);
  free(second_stream);
}

TEST_F(AlsaAddStreamSuite, AppendStreamErrorPropogated) {
  int rc;
  struct cras_rstream *new_stream;
  cras_iodev_append_stream_ret = -10;
  new_stream = (struct cras_rstream *)calloc(1, sizeof(*new_stream));
  rc = thread_add_stream(aio_output_, new_stream);
  EXPECT_EQ(-10, rc);
  free(new_stream);
}

TEST_F(AlsaAddStreamSuite, SimpleAddInputStream) {
  int rc;
  struct cras_rstream *new_stream;
  struct cras_audio_format *fmt;

  cras_alsa_open_called = 0;
  fmt = (struct cras_audio_format *)malloc(sizeof(*fmt));
  memcpy(fmt, &fmt_, sizeof(fmt_));
  aio_input_->base.format = fmt;
  new_stream = (struct cras_rstream *)calloc(1, sizeof(*new_stream));
  new_stream->fd = 55;
  memcpy(&new_stream->format, fmt, sizeof(*fmt));
  rc = thread_add_stream(aio_input_, new_stream);
  ASSERT_EQ(0, rc);
  EXPECT_EQ(55, aio_input_->base.streams->stream->fd);
  EXPECT_EQ(1, cras_alsa_open_called);
  EXPECT_EQ(1, cras_alsa_start_called); //  Shouldn start capture.
  rc = thread_remove_stream(aio_input_, new_stream);
  EXPECT_EQ(0, rc);
  free(new_stream);
}

TEST_F(AlsaAddStreamSuite, OneInputStreamPerDevice) {
  int rc;
  struct cras_rstream *new_stream;

  cras_alsa_open_called = 0;
  new_stream = (struct cras_rstream *)calloc(1, sizeof(*new_stream));
  aio_input_->handle = (snd_pcm_t *)0x01;
  rc = thread_add_stream(aio_input_, new_stream);
  EXPECT_NE(0, rc);
  EXPECT_EQ(0, cras_alsa_open_called);
  free(new_stream);
}

//  Test the audio capture path, this involves a lot of setup before calling the
//  funcitons we want to test.  Will need to setup the device, a fake stream,
//  and a fake shm area to put samples in.
class AlsaCaptureStreamSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      aio_ = (struct alsa_io *)alsa_iodev_create("hw:0,0", fake_mixer,
          CRAS_STREAM_INPUT);
      fmt_.frame_rate = 44100;
      fmt_.num_channels = 2;
      fmt_.format = SND_PCM_FORMAT_S16_LE;
      aio_->base.format = &fmt_;
      aio_->buffer_size = 16384;
      aio_->cb_threshold = 480;

      shm_ = (struct cras_audio_shm_area *)calloc(1,
          sizeof(*shm_) + aio_->cb_threshold * 8);
      shm_->frame_bytes = 4;
      shm_->used_size = aio_->cb_threshold * 4; // channels * bytes/sample
      shm_->size = shm_->used_size * 2;

      rstream_ = (struct cras_rstream *)calloc(1, sizeof(*rstream_));
      rstream_->shm = shm_;
      memcpy(&rstream_->format, &fmt_, sizeof(fmt_));

      cras_iodev_append_stream(&aio_->base, rstream_);

      cras_alsa_mmap_begin_buffer = (uint8_t *)malloc(shm_->used_size);
      cras_alsa_mmap_begin_frames = aio_->cb_threshold;

      ResetStubData();
    }

    virtual void TearDown() {
      free(cras_alsa_mmap_begin_buffer);
      cras_iodev_delete_stream(&aio_->base, rstream_);
      alsa_iodev_destroy((struct cras_iodev *)aio_);
      free(rstream_);
      free(shm_);
    }

  struct alsa_io *aio_;
  struct cras_rstream *rstream_;
  struct cras_audio_format fmt_;
  struct cras_audio_shm_area *shm_;
};

TEST_F(AlsaCaptureStreamSuite, PossiblyReadGetAvailError) {
  struct timespec ts;
  int rc;

  cras_alsa_get_avail_frames_ret = -4;
  rc = possibly_read_audio(aio_, &ts);
  EXPECT_EQ(-4, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_EQ(0, ts.tv_nsec);
}

TEST_F(AlsaCaptureStreamSuite, PossiblyReadEmpty) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  If no samples are present, it should sleep for cb_threshold frames.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = 0;
  nsec_expected = aio_->cb_threshold * 1000000000 / fmt_.frame_rate;
  rc = possibly_read_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
}

TEST_F(AlsaCaptureStreamSuite, PossiblyReadHasDataDrop) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  A full block plus 4 frames.  No streams attached so samples are dropped.
  aio_->base.streams = NULL;
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->cb_threshold + 4;
  nsec_expected = (aio_->cb_threshold - 4) * 1000000000 / fmt_.frame_rate;
  rc = possibly_read_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
}

TEST_F(AlsaCaptureStreamSuite, PossiblyReadHasDataWriteStream) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  A full block plus 4 frames.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->cb_threshold + 4;
  nsec_expected = (aio_->cb_threshold - 4) * 1000000000 / fmt_.frame_rate;
  cras_rstream_audio_ready_count = 999;
  //  Give it some samples to copy.
  rc = possibly_read_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(aio_->cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < aio_->cb_threshold; i++)
    EXPECT_EQ(cras_alsa_mmap_begin_buffer[i], shm_->samples[i]);
}

TEST_F(AlsaCaptureStreamSuite, PossiblyReadWriteTwoBuffers) {
  struct timespec ts;
  int rc;

  //  A full block plus 4 frames.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->cb_threshold + 4;
  cras_rstream_audio_ready_count = 999;
  //  Give it some samples to copy.
  rc = possibly_read_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, shm_->num_overruns);
  EXPECT_EQ(aio_->cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < aio_->cb_threshold; i++)
    EXPECT_EQ(cras_alsa_mmap_begin_buffer[i], shm_->samples[i]);

  cras_rstream_audio_ready_count = 999;
  rc = possibly_read_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, shm_->num_overruns);
  EXPECT_EQ(aio_->cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < aio_->cb_threshold; i++)
    EXPECT_EQ(cras_alsa_mmap_begin_buffer[i],
        shm_->samples[i + shm_->used_size]);
}

TEST_F(AlsaCaptureStreamSuite, PossiblyReadWriteThreeBuffers) {
  struct timespec ts;
  int rc;

  //  A full block plus 4 frames.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->cb_threshold + 4;
  //  Give it some samples to copy.
  rc = possibly_read_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, shm_->num_overruns);
  EXPECT_EQ(aio_->cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < aio_->cb_threshold; i++)
    EXPECT_EQ(cras_alsa_mmap_begin_buffer[i], shm_->samples[i]);

  cras_rstream_audio_ready_count = 999;
  rc = possibly_read_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, shm_->num_overruns);
  EXPECT_EQ(aio_->cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < aio_->cb_threshold; i++)
    EXPECT_EQ(cras_alsa_mmap_begin_buffer[i],
        shm_->samples[i + shm_->used_size]);

  cras_rstream_audio_ready_count = 999;
  rc = possibly_read_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, shm_->num_overruns);  //  Should have overrun.
  EXPECT_EQ(aio_->cb_threshold, cras_rstream_audio_ready_count);
  for (size_t i = 0; i < aio_->cb_threshold; i++)
    EXPECT_EQ(cras_alsa_mmap_begin_buffer[i], shm_->samples[i]);
}

//  Test the audio playback path.
class AlsaPlaybackStreamSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      aio_ = (struct alsa_io *)alsa_iodev_create("hw:0,0", fake_mixer,
          CRAS_STREAM_OUTPUT);
      fmt_.frame_rate = 44100;
      fmt_.num_channels = 2;
      fmt_.format = SND_PCM_FORMAT_S16_LE;
      aio_->base.format = &fmt_;
      aio_->buffer_size = 16384;
      aio_->used_size = 480;
      aio_->cb_threshold = 96;
      aio_->min_cb_level = 240;

      SetupShm(&shm_);
      SetupShm(&shm2_);
      SetupRstream(&rstream_, shm_, 1);
      SetupRstream(&rstream2_, shm2_, 2);

      cras_iodev_append_stream(&aio_->base, rstream_);

      cras_alsa_mmap_begin_buffer = (uint8_t *)malloc(shm_->used_size);
      cras_alsa_mmap_begin_frames = aio_->used_size - aio_->cb_threshold;

      ResetStubData();
    }

    virtual void TearDown() {
      free(cras_alsa_mmap_begin_buffer);
      cras_iodev_delete_stream(&aio_->base, rstream_);
      alsa_iodev_destroy((struct cras_iodev *)aio_);
      free(rstream_);
      free(shm_);
    }

    void SetupShm(struct cras_audio_shm_area **shm) {
      *shm = (struct cras_audio_shm_area *)calloc(1,
          sizeof(**shm) + aio_->used_size * 8);
      (*shm)->frame_bytes = 4;
      (*shm)->used_size = aio_->used_size * 4; //  channels * bytes/sample
      (*shm)->size = (*shm)->used_size * 2;
    }

    void SetupRstream(struct cras_rstream **rstream,
                      struct cras_audio_shm_area *shm,
                      int fd) {
      *rstream = (struct cras_rstream *)calloc(1, sizeof(**rstream));
      (*rstream)->shm = shm;
      memcpy(&(*rstream)->format, &fmt_, sizeof(fmt_));
      (*rstream)->fd = fd;
    }

  struct alsa_io *aio_;
  struct cras_rstream *rstream_;
  struct cras_rstream *rstream2_;
  struct cras_audio_format fmt_;
  struct cras_audio_shm_area *shm_;
  struct cras_audio_shm_area *shm2_;
};

TEST_F(AlsaPlaybackStreamSuite, PossiblyFillGetAvailError) {
  struct timespec ts;
  int rc;

  cras_alsa_get_avail_frames_ret = -4;
  rc = possibly_fill_audio(aio_, &ts);
  EXPECT_EQ(-4, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_EQ(0, ts.tv_nsec);
}

TEST_F(AlsaPlaybackStreamSuite, PossiblyFillEarlyWake) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  If woken and still have tons of data to play, go back to sleep.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->buffer_size - aio_->cb_threshold * 2;
  nsec_expected = (aio_->cb_threshold) * 1000000000 / fmt_.frame_rate;
  rc = possibly_fill_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
}

TEST_F(AlsaPlaybackStreamSuite, PossiblyFillGetFromStreamFull) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  Have cb_threshold samples left.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->buffer_size - aio_->cb_threshold;
  nsec_expected = (aio_->used_size - aio_->cb_threshold) *
      1000000000 / fmt_.frame_rate;

  //  shm has plenty of data in it.
  shm_->write_offset[0] = shm_->used_size;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;

  rc = possibly_fill_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(aio_->used_size - aio_->cb_threshold, cras_mix_add_stream_count);
  EXPECT_EQ(0, cras_rstream_request_audio_called);
  EXPECT_EQ(-1, select_max_fd);
}

TEST_F(AlsaPlaybackStreamSuite, PossiblyFillGetFromStreamFullDoesntMix) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  Have cb_threshold samples left.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->buffer_size - aio_->cb_threshold;
  nsec_expected = (aio_->used_size - aio_->cb_threshold) *
      1000000000 / fmt_.frame_rate;

  //  shm has plenty of data in it.
  shm_->write_offset[0] = shm_->used_size;

  //  Test that nothing breaks if there is an empty stream.
  cras_mix_add_stream_dont_fill_next = 1;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;

  rc = possibly_fill_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_rstream_request_audio_called);
  EXPECT_EQ(-1, select_max_fd);
  EXPECT_EQ(0, shm_->read_offset[0]);
  EXPECT_EQ(0, shm_->read_offset[1]);
  EXPECT_EQ(shm_->used_size, shm_->write_offset[0]);
  EXPECT_EQ(0, shm_->write_offset[1]);
}

TEST_F(AlsaPlaybackStreamSuite, PossiblyFillGetFromStreamNeedFill) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  Have cb_threshold samples left.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->buffer_size - aio_->cb_threshold;
  nsec_expected = (aio_->used_size - aio_->cb_threshold) *
      1000000000 / fmt_.frame_rate;

  //  shm is out of data.
  shm_->write_offset[0] = 0;

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;

  rc = possibly_fill_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(aio_->used_size - aio_->cb_threshold, cras_mix_add_stream_count);
  EXPECT_EQ(1, cras_rstream_request_audio_called);
  EXPECT_NE(-1, select_max_fd);
  EXPECT_EQ(0, memcmp(&select_out_fds, &select_in_fds, sizeof(select_in_fds)));
  EXPECT_EQ(0, shm_->read_offset[0]);
  EXPECT_EQ(0, shm_->write_offset[0]);
}

TEST_F(AlsaPlaybackStreamSuite, PossiblyFillGetFromTwoStreamsFull) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  Have cb_threshold samples left.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->buffer_size - aio_->cb_threshold;
  nsec_expected = (aio_->used_size - aio_->cb_threshold) *
      1000000000 / fmt_.frame_rate;

  //  shm has plenty of data in it.
  shm_->write_offset[0] = shm_->used_size;
  shm2_->write_offset[0] = shm2_->used_size;

  cras_iodev_append_stream(&aio_->base, rstream2_);

  rc = possibly_fill_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(aio_->used_size - aio_->cb_threshold, cras_mix_add_stream_count);
  EXPECT_EQ(0, cras_rstream_request_audio_called);
  EXPECT_EQ(-1, select_max_fd);
}

TEST_F(AlsaPlaybackStreamSuite, PossiblyFillGetFromTwoStreamsFullOneMixes) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;
  size_t written_expected;

  //  Have cb_threshold samples left.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->buffer_size - aio_->cb_threshold;
  written_expected = (aio_->used_size - aio_->cb_threshold);
  nsec_expected = written_expected *
      1000000000 / fmt_.frame_rate;

  //  shm has plenty of data in it.
  shm_->write_offset[0] = shm_->used_size;
  shm2_->write_offset[0] = shm2_->used_size;

  cras_iodev_append_stream(&aio_->base, rstream2_);

  //  Test that nothing breaks if one stream doesn't fill.
  cras_mix_add_stream_dont_fill_next = 1;

  rc = possibly_fill_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_rstream_request_audio_called);
  EXPECT_EQ(0, shm_->read_offset[0]);  //  No write from first stream.
  EXPECT_EQ(written_expected * 4, shm2_->read_offset[0]);
}

TEST_F(AlsaPlaybackStreamSuite, PossiblyFillGetFromTwoStreamsNeedFill) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  Have cb_threshold samples left.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->buffer_size - aio_->cb_threshold;
  nsec_expected = (aio_->used_size - aio_->cb_threshold) *
      1000000000 / fmt_.frame_rate;

  //  shm has nothing left.
  shm_->write_offset[0] = 0;
  shm2_->write_offset[0] = 0;

  cras_iodev_append_stream(&aio_->base, rstream2_);

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  FD_SET(rstream2_->fd, &select_out_fds);
  select_return_value = 2;

  rc = possibly_fill_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(aio_->used_size - aio_->cb_threshold, cras_mix_add_stream_count);
  EXPECT_EQ(2, cras_rstream_request_audio_called);
  EXPECT_NE(-1, select_max_fd);
}

TEST_F(AlsaPlaybackStreamSuite, PossiblyFillGetFromTwoStreamsFillOne) {
  struct timespec ts;
  int rc;
  uint64_t nsec_expected;

  //  Have cb_threshold samples left.
  cras_alsa_get_avail_frames_ret = 0;
  cras_alsa_get_avail_frames_avail = aio_->buffer_size - aio_->cb_threshold;
  nsec_expected = (aio_->used_size - aio_->cb_threshold) *
      1000000000 / fmt_.frame_rate;

  //  One has too little the other is full.
  shm_->write_offset[0] = 40;
  shm_->write_buf_idx = 1;
  shm2_->write_offset[0] = shm2_->used_size;
  shm2_->write_buf_idx = 1;

  cras_iodev_append_stream(&aio_->base, rstream2_);

  FD_ZERO(&select_out_fds);
  FD_SET(rstream_->fd, &select_out_fds);
  select_return_value = 1;

  rc = possibly_fill_audio(aio_, &ts);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, nsec_expected - 1000);
  EXPECT_LE(ts.tv_nsec, nsec_expected + 1000);
  EXPECT_EQ(aio_->used_size - aio_->cb_threshold, cras_mix_add_stream_count);
  EXPECT_EQ(1, cras_rstream_request_audio_called);
  EXPECT_NE(-1, select_max_fd);
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

int cras_iodev_append_stream(struct cras_iodev *dev,
			     struct cras_rstream *stream)
{
  struct cras_io_stream *out;

  if (cras_iodev_append_stream_ret) {
    int rc = cras_iodev_append_stream_ret;
    cras_iodev_append_stream_ret = 0;
    return rc;
  }

  /* New stream, allocate a container and add it to the list. */
  out = (struct cras_io_stream *)calloc(1, sizeof(*out));
  if (out == NULL)
    return -ENOMEM;
  out->stream = stream;
  out->shm = cras_rstream_get_shm(stream);
  out->fd = cras_rstream_get_audio_fd(stream);
  DL_APPEND(dev->streams, out);

  return 0;
}
int cras_iodev_delete_stream(struct cras_iodev *dev,
			     struct cras_rstream *stream)
{
  struct cras_io_stream *out;

  /* Find stream, and if found, delete it. */
  DL_SEARCH_SCALAR(dev->streams, out, stream, stream);
  if (out == NULL)
    return -EINVAL;
  DL_DELETE(dev->streams, out);
  free(out);

  return 0;
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
int cras_alsa_mmap_begin(snd_pcm_t *handle, size_t format_bytes,
			 uint8_t **dst, snd_pcm_uframes_t *offset,
			 snd_pcm_uframes_t *frames, size_t *underruns)
{
  *dst = cras_alsa_mmap_begin_buffer;
  *frames = cras_alsa_mmap_begin_frames;
  return 0;
}
int cras_alsa_mmap_commit(snd_pcm_t *handle, snd_pcm_uframes_t offset,
			  snd_pcm_uframes_t frames, size_t *underruns)
{
  return 0;
}

//  From util.
int cras_set_rt_scheduling(int rt_lim)
{
  return 0;
}
int cras_set_thread_priority(int priority)
{
  return 0;
}

//  From rstream.
int cras_rstream_request_audio(const struct cras_rstream *stream, size_t count)
{
  cras_rstream_request_audio_called++;
  return 0;
}
int cras_rstream_request_audio_buffer(const struct cras_rstream *stream)
{
  return 0;
}
int cras_rstream_get_audio_request_reply(const struct cras_rstream *stream)
{
  return 0;
}
int cras_rstream_audio_ready(const struct cras_rstream *stream, size_t count)
{
  cras_rstream_audio_ready_count = count;
  return 0;
}

//  ALSA stubs.
int snd_pcm_format_physical_width(snd_pcm_format_t format)
{
  return 16;
}

const char *snd_strerror(int errnum)
{
  return "Alsa Error in UT";
}

//  From librt.
int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
  tp->tv_sec = clock_gettime_retspec.tv_sec;
  tp->tv_nsec = clock_gettime_retspec.tv_nsec;
  return 0;
}

//  From mixer.
size_t cras_mix_add_stream(struct cras_audio_shm_area *shm,
			   size_t num_channels,
			   uint8_t *dst,
			   size_t *count,
			   size_t *index)
{
  if (cras_mix_add_stream_dont_fill_next) {
    cras_mix_add_stream_dont_fill_next = 0;
    return 0;
  }
  cras_mix_add_stream_count = *count;
  *index = *index + 1;
  return *count;
}

//  From alsa_mixer.
struct cras_alsa_mixer *cras_alsa_mixer_create(const char *card_name)
{
  return mixer_create_return_value;
}

void cras_alsa_mixer_destroy(struct cras_alsa_mixer *m)
{
  mixer_destroy_value = m;
  mixer_destroy_called++;
}

//  From system_settings.
size_t cras_system_get_volume()
{
  sys_get_volume_called++;
  return sys_get_volume_return_value;
}

void cras_system_register_volume_changed_cb(cras_system_volume_changed_cb cb,
					    void *arg)
{
  sys_register_volume_cb_called++;
  sys_register_volume_cb_value = cb;
  sys_register_volume_cb_arg = arg;
}

int cras_system_get_mute()
{
  sys_get_mute_called++;
  return sys_get_mute_return_value;
}

void cras_system_register_mute_changed_cb(cras_system_mute_changed_cb cb,
					  void *arg)
{
  sys_register_mute_cb_called++;
  sys_register_mute_cb_value = cb;
  sys_register_mute_cb_arg = arg;
}

//  From cras_alsa_mixer.
void cras_alsa_mixer_set_volume(struct cras_alsa_mixer *m, long dB_level)
{
  alsa_mixer_set_volume_called++;
  alsa_mixer_set_volume_value = dB_level;
}

void cras_alsa_mixer_set_mute(struct cras_alsa_mixer *m, int mute)
{
  alsa_mixer_set_mute_called++;
  alsa_mixer_set_mute_value = mute;
}

//  From cras_volume_curve.
long cras_volume_curve_get_db_for_index(size_t volume)
{
	return 100 * (volume - 100);
}

}

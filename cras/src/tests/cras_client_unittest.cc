// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <sys/shm.h>
#include <gtest/gtest.h>

extern "C" {

#include "cras_fmt_conv.h"
#include "cras_messages.h"

//  Include C file to test static functions.
#include "cras_client.c"
}

static const cras_stream_id_t FIRST_STREAM_ID = 1;

static float conv_out_frames_to_in_ratio;
static size_t cras_fmt_conv_convert_frames_in_frames_val;
static size_t cras_fmt_conv_convert_frames_out_frames_val;

static int shmat_called;
static int shmdt_called;
static int shmget_called;
static int close_called;
static int pipe_called;

static void* shmat_returned_value;
static int pthread_create_returned_value;

namespace {

struct fake_cras_fmt_conv {
  struct cras_audio_format in_format;
  struct cras_audio_format out_format;
};

static struct fake_cras_fmt_conv fake_conv;

void InitStaticVariables() {
  shmat_called = 0;
  shmdt_called = 0;
  shmget_called = 0;
  close_called = 0;
  pipe_called = 0;
  shmat_returned_value = NULL;
  pthread_create_returned_value = 0;
}

class CrasClientTestSuite : public testing::Test {
  protected:

    void InitShm(struct cras_audio_shm* shm) {
      shm->area = static_cast<cras_audio_shm_area*>(
          calloc(1, sizeof(*shm->area)));
      cras_shm_set_frame_bytes(shm, 4);
      cras_shm_set_used_size(shm, shm_writable_frames_ * 4);
      memcpy(&shm->area->config, &shm->config, sizeof(shm->config));
    }

    void FreeShm(struct cras_audio_shm* shm) {
      if (shm->area) {
        free(shm->area);
        shm->area = NULL;
      }
    }

    virtual void SetUp() {
      shm_writable_frames_ = 100;
      InitStaticVariables();

      memset(&stream_, 0, sizeof(stream_));
      stream_.id = FIRST_STREAM_ID;

      struct cras_stream_params* config =
          static_cast<cras_stream_params*>(calloc(1, sizeof(*config)));
      config->buffer_frames = 1024;
      config->cb_threshold = 512;
      config->min_cb_level = 512;
      stream_.config = config;
    }

    virtual void TearDown() {
      if (stream_.config) {
        free(stream_.config);
        stream_.config = NULL;
      }
    }

    void StreamConnected(CRAS_STREAM_DIRECTION direction);

    void StreamConnectedFail(CRAS_STREAM_DIRECTION direction);

    struct client_stream stream_;
    int shm_writable_frames_;
};

TEST_F(CrasClientTestSuite, ConfigPlaybackBuf) {
  uint8_t *playback_frames;
  unsigned int fr;

  InitShm(&stream_.play_shm);
  stream_.play_conv = reinterpret_cast<cras_fmt_conv*>(&fake_conv);

  /* Convert from 48kHz to 16kHz */
  conv_out_frames_to_in_ratio = 3.0f;

  /* Expect configured frames not limited by shm */
  fr = config_playback_buf(&stream_, &playback_frames, 100);
  ASSERT_EQ(fr, 300);

  /* Expect configured frames limited by shm limit */
  fr = config_playback_buf(&stream_, &playback_frames, 300);
  ASSERT_EQ(fr, shm_writable_frames_ * conv_out_frames_to_in_ratio);

  /* Expect configured frames limited by shm min_cb_level as well */
  shm_writable_frames_ = 300;
  cras_shm_set_used_size(&stream_.play_shm, shm_writable_frames_ * 4);
  fr = config_playback_buf(&stream_, &playback_frames, 300);
  ASSERT_EQ(fr, stream_.config->min_cb_level);

  FreeShm(&stream_.play_shm);
}

TEST_F(CrasClientTestSuite, CompletePlaybackWrite) {
  /* Test complete playback with different frames, format
   * converter should handle the converted frame count
   * and limit. */

  InitShm(&stream_.play_shm);
  stream_.play_conv = reinterpret_cast<cras_fmt_conv*>(&fake_conv);

  complete_playback_write(&stream_, 200);
  ASSERT_EQ(cras_fmt_conv_convert_frames_in_frames_val, 200);
  ASSERT_EQ(cras_fmt_conv_convert_frames_out_frames_val,
            shm_writable_frames_);

  /* Complete playback with frames larger then shm limit */
  complete_playback_write(&stream_, 400);
  ASSERT_EQ(cras_fmt_conv_convert_frames_in_frames_val, 400);
  ASSERT_EQ(cras_fmt_conv_convert_frames_out_frames_val,
            shm_writable_frames_);

  FreeShm(&stream_.play_shm);
}

void set_audio_format(struct cras_audio_format* format,
                      snd_pcm_format_t pcm_format,
                      size_t frame_rate,
                      size_t num_channels) {
  format->format = pcm_format;
  format->frame_rate = frame_rate;
  format->num_channels = num_channels;
  for (size_t i = 0; i < CRAS_CH_MAX; ++i)
    format->channel_layout[i] = i < num_channels ? i : -1;
}

bool cras_audio_format_equal(struct cras_audio_format& a,
                             struct cras_audio_format& b) {
  if (a.format != b.format || a.frame_rate != b.frame_rate)
    return false;
  for (size_t i = 0; i < CRAS_CH_MAX; ++i)
    if (a.channel_layout[i] != b.channel_layout[i])
      return false;
  return true;
}


void CrasClientTestSuite::StreamConnected(CRAS_STREAM_DIRECTION direction) {
  struct cras_client_stream_connected msg;
  int input_shm_key = 0;
  int output_shm_key = 1;
  int shm_max_size = 600;
  size_t format_bytes;
  struct cras_audio_shm_area area;

  stream_.direction = direction;
  set_audio_format(&stream_.config->format, SND_PCM_FORMAT_S16_LE, 48000, 4);

  struct cras_audio_format server_format;
  set_audio_format(&server_format, SND_PCM_FORMAT_S16_LE, 44100, 2);

  // Initialize shm area
  format_bytes = cras_get_format_bytes(&server_format);
  memset(&area, 0, sizeof(area));
  area.config.frame_bytes = format_bytes;
  area.config.used_size = shm_writable_frames_ * format_bytes;

  shmat_returned_value = &area;

  cras_fill_client_stream_connected(
      &msg,
      0,
      stream_.id,
      server_format,
      input_shm_key,
      output_shm_key,
      shm_max_size);

  stream_connected(&stream_, &msg);

  EXPECT_EQ(1, shmget_called);
  EXPECT_EQ(1, shmat_called);
  EXPECT_NE(0, stream_.thread.running);

  if (direction == CRAS_STREAM_OUTPUT) {
    EXPECT_EQ(NULL, stream_.capture_shm.area);
    EXPECT_EQ(&area, stream_.play_shm.area);
    EXPECT_EQ(reinterpret_cast<cras_fmt_conv*>(&fake_conv), stream_.play_conv);
    EXPECT_TRUE(cras_audio_format_equal(stream_.config->format,
                                        fake_conv.in_format));
    EXPECT_TRUE(cras_audio_format_equal(server_format, fake_conv.out_format));
  } else {
    EXPECT_EQ(NULL, stream_.play_shm.area);
    EXPECT_EQ(&area, stream_.capture_shm.area);
    EXPECT_EQ(reinterpret_cast<cras_fmt_conv*>(&fake_conv),
              stream_.capture_conv);
    EXPECT_TRUE(cras_audio_format_equal(stream_.config->format,
                                        fake_conv.out_format));
    EXPECT_TRUE(cras_audio_format_equal(server_format, fake_conv.in_format));
  }
}

TEST_F(CrasClientTestSuite, InputStreamConnected) {
  StreamConnected(CRAS_STREAM_INPUT);
}

TEST_F(CrasClientTestSuite, OutputStreamConnected) {
  StreamConnected(CRAS_STREAM_OUTPUT);
}

void CrasClientTestSuite::StreamConnectedFail(
    CRAS_STREAM_DIRECTION direction) {

  struct cras_client_stream_connected msg;
  int input_shm_key = 0;
  int output_shm_key = 1;
  int shm_max_size = 600;
  size_t format_bytes;
  struct cras_audio_shm_area area;

  stream_.direction = direction;
  set_audio_format(&stream_.config->format, SND_PCM_FORMAT_S16_LE, 48000, 4);

  struct cras_audio_format server_format;
  set_audio_format(&server_format, SND_PCM_FORMAT_S16_LE, 44100, 2);

  // Initialize shm area
  format_bytes = cras_get_format_bytes(&server_format);
  memset(&area, 0, sizeof(area));
  area.config.frame_bytes = format_bytes;
  area.config.used_size = shm_writable_frames_ * format_bytes;

  shmat_returned_value = &area;

  // let pthread_create fail
  pthread_create_returned_value = -1;

  cras_fill_client_stream_connected(
      &msg,
      0,
      stream_.id,
      server_format,
      input_shm_key,
      output_shm_key,
      shm_max_size);

  stream_connected(&stream_, &msg);

  EXPECT_EQ(0, stream_.thread.running);
  EXPECT_EQ(1, shmget_called);
  EXPECT_EQ(1, shmat_called);
  EXPECT_EQ(1, shmdt_called);
  EXPECT_EQ(1, pipe_called);
  EXPECT_EQ(2, close_called); // close the pipefds
}

TEST_F(CrasClientTestSuite, InputStreamConnectedFail) {
  StreamConnectedFail(CRAS_STREAM_INPUT);
}

TEST_F(CrasClientTestSuite, OutputStreamConnectedFail) {
  StreamConnectedFail(CRAS_STREAM_OUTPUT);
}

} // namepsace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/* stubs */
extern "C" {

int shmget(key_t key, size_t size, int shmflg) {
  ++shmget_called;
  return 0;
}

void* shmat(int shmid, const void* shmaddr, int shmflg) {
  ++shmat_called;
  return shmat_returned_value;
}

int shmdt(const void *shmaddr) {
  ++shmdt_called;
  return 0;
}

int pipe(int pipefd[2]) {
  pipefd[0] = 1;
  pipefd[1] = 2;
  ++pipe_called;
  return 0;
}

int close(int fd) {
  ++close_called;
  return 0;
}

int pthread_create(pthread_t *thread,
                   const pthread_attr_t *attr,
                   void *(*start_routine)(void *),
                   void *arg) {
  return pthread_create_returned_value;
}

size_t cras_fmt_conv_out_frames_to_in(struct cras_fmt_conv *conv,
				      size_t out_frames) {
  return out_frames * conv_out_frames_to_in_ratio;
}

void cras_fmt_conv_destroy(struct cras_fmt_conv *conv)
{
}

struct cras_fmt_conv *cras_fmt_conv_create(const struct cras_audio_format *in,
					   const struct cras_audio_format *out,
					   size_t max_frames)
{
  fake_conv.in_format = *in;
  fake_conv.out_format = *out;
  return reinterpret_cast<struct cras_fmt_conv*>(&fake_conv);
}

size_t cras_fmt_conv_convert_frames(struct cras_fmt_conv *conv,
				    uint8_t *in_buf,
				    uint8_t *out_buf,
				    size_t in_frames,
				    size_t out_frames)
{
  cras_fmt_conv_convert_frames_in_frames_val = in_frames;
  cras_fmt_conv_convert_frames_out_frames_val = out_frames;

  /* Don't care the return value */
  return 0;
}

int cras_fmt_conversion_needed(const struct cras_audio_format *a,
			       const struct cras_audio_format *b)
{
  return 1;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
  tp->tv_sec = 0;
  tp->tv_nsec = 0;
  return 0;
}
}

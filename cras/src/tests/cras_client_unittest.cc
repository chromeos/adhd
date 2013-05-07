// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <sys/shm.h>
#include <gtest/gtest.h>

extern "C" {

#include "cras_fmt_conv.h"

//  Include C file to test static functions.
#include "cras_client.c"
}

static float conv_out_frames_to_in_ratio;
static size_t cras_fmt_conv_convert_frames_in_frames_val;
static size_t cras_fmt_conv_convert_frames_out_frames_val;
static struct cras_fmt_conv *fake_conv =
    reinterpret_cast<struct cras_fmt_conv *>(0x123);

namespace {

class CrasClientTestSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      struct cras_audio_shm *shm;
      struct cras_stream_params **config;

      shm_writable_frames_ = 256;

      /* config */
      memset(&stream_, 0, sizeof(stream_));
      config = &stream_.config;
      *config = static_cast<cras_stream_params *>(
              calloc(1, sizeof(**config)));
      memset(*config, 0, sizeof(**config));
      (*config)->buffer_frames = 1024;
      (*config)->cb_threshold = 512;
      (*config)->min_cb_level = 512;

      /* shm */
      shm = &stream_.play_shm;
      memset(shm, 0, sizeof(*shm));
      shm->area = static_cast<cras_audio_shm_area *>(
              calloc(1, sizeof(*shm->area)));

      cras_shm_set_frame_bytes(shm, 4);
      cras_shm_set_used_size(shm, shm_writable_frames_ * 4);
      memcpy(&shm->area->config, &shm->config, sizeof(shm->config));

      /* play conv */
      stream_.play_conv = fake_conv;
    }

    virtual void TearDown() {
      free(stream_.play_shm.area);
      free(stream_.config);
    }

    struct client_stream stream_;
    int shm_writable_frames_;
};

TEST_F(CrasClientTestSuite, ConfigPlaybackBuf) {
  uint8_t *playback_frames;
  unsigned int fr;

  /* Convert from 48kHz to 16kHz */
  conv_out_frames_to_in_ratio = 3.0f;

  /* Expect configured frames not limited by shm */
  fr = config_playback_buf(&stream_, &playback_frames, 200);
  ASSERT_EQ(fr, 600);

  /* Expect configured frames limited by shm limit */
  fr = config_playback_buf(&stream_, &playback_frames, 300);
  ASSERT_EQ(fr, shm_writable_frames_ * conv_out_frames_to_in_ratio);
}

TEST_F(CrasClientTestSuite, CompletePlaybackWrite) {
  /* Test complete playback with different frames, format
   * converter should handle the converted frame count
   * and limit. */
  complete_playback_write(&stream_, 200);
  ASSERT_EQ(cras_fmt_conv_convert_frames_in_frames_val, 200);
  ASSERT_EQ(cras_fmt_conv_convert_frames_out_frames_val,
            shm_writable_frames_);

  /* Complete playback with frames larger then shm limit */
  complete_playback_write(&stream_, 400);
  ASSERT_EQ(cras_fmt_conv_convert_frames_in_frames_val, 400);
  ASSERT_EQ(cras_fmt_conv_convert_frames_out_frames_val,
            shm_writable_frames_);
}

} // namepsace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/* stubs */
extern "C" {

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
  return fake_conv;
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

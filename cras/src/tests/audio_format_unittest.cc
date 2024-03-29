// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

extern "C" {
// Test static functions
#include "cras/src/common/cras_audio_format.c"
}

namespace {

class ChannelConvMtxTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    int i;
    in_fmt = cras_audio_format_create(SND_PCM_FORMAT_S16_LE, 44100, 6);
    out_fmt = cras_audio_format_create(SND_PCM_FORMAT_S16_LE, 44100, 6);
    for (i = 0; i < CRAS_CH_MAX; i++) {
      in_fmt->channel_layout[i] = -1;
      out_fmt->channel_layout[i] = -1;
    }
  }

  virtual void TearDown() {
    cras_audio_format_destroy(in_fmt);
    cras_audio_format_destroy(out_fmt);
    if (conv_mtx) {
      cras_channel_conv_matrix_destroy(conv_mtx, 6);
    }
  }

  struct cras_audio_format* in_fmt;
  struct cras_audio_format* out_fmt;
  float** conv_mtx;
};

TEST_F(ChannelConvMtxTestSuite, MatrixCreateSuccess) {
  in_fmt->channel_layout[0] = 5;
  in_fmt->channel_layout[1] = 4;
  in_fmt->channel_layout[2] = 3;
  in_fmt->channel_layout[3] = 2;
  in_fmt->channel_layout[4] = 1;
  in_fmt->channel_layout[5] = 0;

  out_fmt->channel_layout[0] = 0;
  out_fmt->channel_layout[1] = 1;
  out_fmt->channel_layout[2] = 2;
  out_fmt->channel_layout[3] = 3;
  out_fmt->channel_layout[4] = 4;
  out_fmt->channel_layout[5] = 5;

  conv_mtx = cras_channel_conv_matrix_create(in_fmt, out_fmt);
  ASSERT_NE(conv_mtx, (void*)NULL);
}

TEST_F(ChannelConvMtxTestSuite, MatrixCreateSuccess2) {
  in_fmt->channel_layout[0] = 5;
  in_fmt->channel_layout[1] = 4;
  in_fmt->channel_layout[2] = 3;
  in_fmt->channel_layout[3] = 2;
  in_fmt->channel_layout[4] = 1;
  in_fmt->channel_layout[5] = 0;

  out_fmt->channel_layout[0] = 0;
  out_fmt->channel_layout[1] = 1;
  out_fmt->channel_layout[2] = 2;
  out_fmt->channel_layout[3] = 3;
  out_fmt->channel_layout[4] = 4;
  out_fmt->channel_layout[7] = 5;

  conv_mtx = cras_channel_conv_matrix_create(in_fmt, out_fmt);
  ASSERT_NE(conv_mtx, (void*)NULL);
}

TEST_F(ChannelConvMtxTestSuite, MatrixCreateMissingCRAS_CH_FC) {
  in_fmt->channel_layout[0] = 5;
  in_fmt->channel_layout[1] = 4;
  in_fmt->channel_layout[2] = 3;
  in_fmt->channel_layout[3] = 2;
  in_fmt->channel_layout[4] = 1;
  in_fmt->channel_layout[5] = 0;

  out_fmt->channel_layout[0] = 0;
  out_fmt->channel_layout[1] = 1;
  out_fmt->channel_layout[2] = 2;
  out_fmt->channel_layout[3] = 3;
  out_fmt->channel_layout[6] = 4;
  out_fmt->channel_layout[7] = 5;

  conv_mtx = cras_channel_conv_matrix_create(in_fmt, out_fmt);
  ASSERT_EQ(conv_mtx, (void*)NULL);
}

TEST_F(ChannelConvMtxTestSuite, SLSRToRRRL) {
  in_fmt->channel_layout[0] = 0;
  in_fmt->channel_layout[1] = 1;
  in_fmt->channel_layout[4] = 2;
  in_fmt->channel_layout[5] = 3;
  // Input format uses SL and SR
  in_fmt->channel_layout[6] = 4;
  in_fmt->channel_layout[7] = 5;

  out_fmt->channel_layout[0] = 0;
  out_fmt->channel_layout[1] = 1;
  // Output format uses RR and RR
  out_fmt->channel_layout[2] = 4;
  out_fmt->channel_layout[3] = 5;
  out_fmt->channel_layout[4] = 2;
  out_fmt->channel_layout[5] = 3;

  conv_mtx = cras_channel_conv_matrix_create(in_fmt, out_fmt);
  ASSERT_NE(conv_mtx, (void*)NULL);
}

TEST(AudioFormat, GetMinNumChannelsDefault) {
  struct cras_audio_format* fmt =
      cras_audio_format_create(SND_PCM_FORMAT_S16_LE, 48000, 6);
  ASSERT_EQ(cras_audio_format_get_least_num_channels(fmt), 6);
  cras_audio_format_destroy(fmt);
}

TEST(AudioFormat, GetMinNumChannelsNonDefault) {
  struct cras_audio_format* fmt =
      cras_audio_format_create(SND_PCM_FORMAT_S16_LE, 48000, 4);
  const int8_t layout[CRAS_CH_MAX] = {2, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  cras_audio_format_set_channel_layout(fmt, layout);
  ASSERT_EQ(cras_audio_format_get_least_num_channels(fmt), 3);
  cras_audio_format_destroy(fmt);
}

TEST(AudioFormat, GetMinNumChannelsAllUndefined) {
  struct cras_audio_format* fmt =
      cras_audio_format_create(SND_PCM_FORMAT_S16_LE, 48000, 2);
  const int8_t layout[CRAS_CH_MAX] = {-1, -1, -1, -1, -1, -1,
                                      -1, -1, -1, -1, -1};
  cras_audio_format_set_channel_layout(fmt, layout);
  ASSERT_EQ(cras_audio_format_get_least_num_channels(fmt), 0);
  cras_audio_format_destroy(fmt);
}

}  // namespace

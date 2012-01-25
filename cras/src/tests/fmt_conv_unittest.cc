// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

extern "C" {
#include "cras_fmt_conv.h"
#include "cras_types.h"
}

// Don't yet support format conversion.
TEST(FormatConverterTest,  InvalidParamsDifferentFormats) {
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;
  struct cras_fmt_conv *c;

  in_fmt.format = SND_PCM_FORMAT_S16_LE;
  out_fmt.format = SND_PCM_FORMAT_S32_LE;
  in_fmt.num_channels = out_fmt.num_channels = 2;
  in_fmt.frame_rate = 96000;
  out_fmt.frame_rate = 48000;
  c = cras_fmt_conv_create(&in_fmt, &out_fmt, 4096);
  EXPECT_EQ(NULL, c);
}

// Don't yet support up/down mix.
TEST(FormatConverterTest,  InvalidParamsUpDownMix) {
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;
  struct cras_fmt_conv *c;

  in_fmt.format = out_fmt.format = SND_PCM_FORMAT_S16_LE;
  in_fmt.num_channels = 4;
  out_fmt.num_channels = 2;
  c = cras_fmt_conv_create(&in_fmt, &out_fmt, 4096);
  EXPECT_EQ(NULL, c);
}

// Only support S16LE, S32 should fail.
TEST(FormatConverterTest,  InvalidParamsOnlyS16LE) {
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;
  struct cras_fmt_conv *c;

  in_fmt.format = out_fmt.format = SND_PCM_FORMAT_S32_LE;
  in_fmt.num_channels = out_fmt.num_channels = 2;
  c = cras_fmt_conv_create(&in_fmt, &out_fmt, 4096);
  EXPECT_EQ(NULL, c);
}

// Test 2 to 1 SRC.
TEST(FormatConverterTest,  Convert2To1) {
  struct cras_fmt_conv *c;
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;

  size_t out_frames;
  uint8_t *in_buff;
  uint8_t *out_buff;
  const size_t buf_size = 4096;

  in_fmt.format = out_fmt.format = SND_PCM_FORMAT_S16_LE;
  in_fmt.num_channels = out_fmt.num_channels = 2;
  in_fmt.frame_rate = 96000;
  out_fmt.frame_rate = 48000;

  c = cras_fmt_conv_create(&in_fmt, &out_fmt, buf_size);
  ASSERT_NE(c, (void *)NULL);

  out_frames = cras_fmt_conv_in_frames_to_out(c, buf_size);
  EXPECT_EQ(buf_size/2, out_frames);

  in_buff = cras_fmt_conv_get_buffer(c);
  EXPECT_NE(in_buff, (void *)NULL);
  out_buff = (uint8_t *)malloc(buf_size/2 * cras_get_format_bytes(&out_fmt));
  out_frames = cras_fmt_conv_convert_to(c, out_buff, buf_size);
  EXPECT_EQ(buf_size/2, out_frames);

  cras_fmt_conv_destroy(c);
}

// Test 1 to 2 SRC.
TEST(FormatConverterTest,  Convert1To2) {
  struct cras_fmt_conv *c;
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;
  size_t out_frames;
  uint8_t *in_buff;
  uint8_t *out_buff;
  const size_t buf_size = 4096;

  in_fmt.format = out_fmt.format = SND_PCM_FORMAT_S16_LE;
  in_fmt.num_channels = out_fmt.num_channels = 2;
  in_fmt.frame_rate = 22050;
  out_fmt.frame_rate = 44100;

  c = cras_fmt_conv_create(&in_fmt, &out_fmt, buf_size);
  ASSERT_NE(c, (void *)NULL);

  out_frames = cras_fmt_conv_in_frames_to_out(c, buf_size);
  EXPECT_EQ(buf_size*2, out_frames);

  in_buff = cras_fmt_conv_get_buffer(c);
  EXPECT_NE(in_buff, (void *)NULL);
  out_buff = (uint8_t *)malloc(buf_size*2 * cras_get_format_bytes(&out_fmt));
  out_frames = cras_fmt_conv_convert_to(c, out_buff, buf_size);
  EXPECT_EQ(buf_size*2, out_frames);

  cras_fmt_conv_destroy(c);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

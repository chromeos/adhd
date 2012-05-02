// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

extern "C" {
#include "cras_fmt_conv.h"
#include "cras_types.h"
}

// Don't yet support format conversion to anything but S16LE.
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

// Only support LE, BE should fail.
TEST(FormatConverterTest,  InvalidParamsOnlyLE) {
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;
  struct cras_fmt_conv *c;

  in_fmt.format = out_fmt.format = SND_PCM_FORMAT_S32_BE;
  in_fmt.num_channels = out_fmt.num_channels = 2;
  c = cras_fmt_conv_create(&in_fmt, &out_fmt, 4096);
  EXPECT_EQ(NULL, c);
}

// Test Mono to Stereo mix.
TEST(FormatConverterTest, MonoToStereo) {
  struct cras_fmt_conv *c;
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;

  size_t out_frames;
  int16_t *in_buff;
  int16_t *out_buff;
  const size_t buf_size = 4096;

  in_fmt.format = SND_PCM_FORMAT_S16_LE;
  out_fmt.format = SND_PCM_FORMAT_S16_LE;
  in_fmt.num_channels = 1;
  out_fmt.num_channels = 2;
  in_fmt.frame_rate = 48000;
  out_fmt.frame_rate = 48000;

  c = cras_fmt_conv_create(&in_fmt, &out_fmt, buf_size);
  ASSERT_NE(c, (void *)NULL);

  out_frames = cras_fmt_conv_out_frames_to_in(c, buf_size);
  EXPECT_EQ(buf_size, out_frames);

  out_frames = cras_fmt_conv_in_frames_to_out(c, buf_size);
  EXPECT_EQ(buf_size, out_frames);

  in_buff = (int16_t *)cras_fmt_conv_get_buffer(c);
  EXPECT_NE(in_buff, (void *)NULL);
  out_buff = (int16_t *)malloc(buf_size * 2 * cras_get_format_bytes(&out_fmt));
  out_frames = cras_fmt_conv_convert_to(c,
                                        (uint8_t *)out_buff,
                                        buf_size);
  EXPECT_EQ(buf_size, out_frames);
  for (size_t i = 0; i < buf_size; i++) {
    if (in_buff[i] != out_buff[i*2] ||
        in_buff[i] != out_buff[i*2 + 1]) {
      EXPECT_TRUE(false);
      break;
    }
  }

  cras_fmt_conv_destroy(c);
}

// Test 5.1 to Stereo mix.
TEST(FormatConverterTest, SurroundToStereo) {
  struct cras_fmt_conv *c;
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;

  size_t out_frames;
  int16_t *in_buff;
  int16_t *out_buff;
  const size_t buf_size = 4096;

  in_fmt.format = SND_PCM_FORMAT_S16_LE;
  out_fmt.format = SND_PCM_FORMAT_S16_LE;
  in_fmt.num_channels = 6;
  out_fmt.num_channels = 2;
  in_fmt.frame_rate = 48000;
  out_fmt.frame_rate = 48000;

  c = cras_fmt_conv_create(&in_fmt, &out_fmt, buf_size);
  ASSERT_NE(c, (void *)NULL);

  out_frames = cras_fmt_conv_out_frames_to_in(c, buf_size);
  EXPECT_EQ(buf_size, out_frames);

  out_frames = cras_fmt_conv_in_frames_to_out(c, buf_size);
  EXPECT_EQ(buf_size, out_frames);

  in_buff = (int16_t *)cras_fmt_conv_get_buffer(c);
  EXPECT_NE(in_buff, (void *)NULL);
  out_buff = (int16_t *)malloc(buf_size * 2 * cras_get_format_bytes(&out_fmt));
  out_frames = cras_fmt_conv_convert_to(c,
                                        (uint8_t *)out_buff,
                                        buf_size);
  EXPECT_EQ(buf_size, out_frames);

  cras_fmt_conv_destroy(c);
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

// Test 1 to 2 SRC with mono to stereo conversion.
TEST(FormatConverterTest,  Convert1To2MonoToStereo) {
  struct cras_fmt_conv *c;
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;
  size_t out_frames;
  uint8_t *in_buff;
  uint8_t *out_buff;
  const size_t buf_size = 4096;

  in_fmt.format = out_fmt.format = SND_PCM_FORMAT_S16_LE;
  in_fmt.num_channels = 1;
  out_fmt.num_channels = 2;
  in_fmt.frame_rate = 22050;
  out_fmt.frame_rate = 44100;

  c = cras_fmt_conv_create(&in_fmt, &out_fmt, buf_size);
  ASSERT_NE(c, (void *)NULL);

  out_frames = cras_fmt_conv_out_frames_to_in(c, buf_size);
  EXPECT_EQ(buf_size / 2, out_frames);

  out_frames = cras_fmt_conv_in_frames_to_out(c, buf_size);
  EXPECT_EQ(buf_size * 2, out_frames);

  in_buff = cras_fmt_conv_get_buffer(c);
  EXPECT_NE(in_buff, (void *)NULL);
  out_buff = (uint8_t *)malloc(buf_size * 2 * cras_get_format_bytes(&out_fmt));
  out_frames = cras_fmt_conv_convert_to(c, out_buff, buf_size);
  EXPECT_EQ(buf_size * 2, out_frames);

  cras_fmt_conv_destroy(c);
}

// Test 32 to 16 bit conversion.
TEST(FormatConverterTest, ConvertS32LEToS16LE) {
  struct cras_fmt_conv *c;
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;

  size_t out_frames;
  int32_t *in_buff;
  int16_t *out_buff;
  const size_t buf_size = 4096;

  in_fmt.format = SND_PCM_FORMAT_S32_LE;
  out_fmt.format = SND_PCM_FORMAT_S16_LE;
  in_fmt.num_channels = out_fmt.num_channels = 2;
  in_fmt.frame_rate = 48000;
  out_fmt.frame_rate = 48000;

  c = cras_fmt_conv_create(&in_fmt, &out_fmt, buf_size);
  ASSERT_NE(c, (void *)NULL);

  out_frames = cras_fmt_conv_in_frames_to_out(c, buf_size);
  EXPECT_EQ(buf_size, out_frames);

  in_buff = (int32_t *)cras_fmt_conv_get_buffer(c);
  EXPECT_NE(in_buff, (void *)NULL);
  out_buff = (int16_t *)malloc(buf_size * cras_get_format_bytes(&out_fmt));
  out_frames = cras_fmt_conv_convert_to(c, (uint8_t *)out_buff, buf_size);
  EXPECT_EQ(buf_size, out_frames);
  for (unsigned int i = 0; i < buf_size; i++)
	  EXPECT_EQ((int16_t)(in_buff[i] >> 16), out_buff[i]);

  cras_fmt_conv_destroy(c);
}

// Test 24 to 16 bit conversion.
TEST(FormatConverterTest, ConvertS24LEToS16LE) {
  struct cras_fmt_conv *c;
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;

  size_t out_frames;
  int32_t *in_buff;
  int16_t *out_buff;
  const size_t buf_size = 4096;

  in_fmt.format = SND_PCM_FORMAT_S24_LE;
  out_fmt.format = SND_PCM_FORMAT_S16_LE;
  in_fmt.num_channels = out_fmt.num_channels = 2;
  in_fmt.frame_rate = 48000;
  out_fmt.frame_rate = 48000;

  c = cras_fmt_conv_create(&in_fmt, &out_fmt, buf_size);
  ASSERT_NE(c, (void *)NULL);

  out_frames = cras_fmt_conv_in_frames_to_out(c, buf_size);
  EXPECT_EQ(buf_size, out_frames);

  in_buff = (int32_t *)cras_fmt_conv_get_buffer(c);
  EXPECT_NE(in_buff, (void *)NULL);
  out_buff = (int16_t *)malloc(buf_size * cras_get_format_bytes(&out_fmt));
  out_frames = cras_fmt_conv_convert_to(c, (uint8_t *)out_buff, buf_size);
  EXPECT_EQ(buf_size, out_frames);
  for (unsigned int i = 0; i < buf_size; i++)
	  EXPECT_EQ((int16_t)(in_buff[i] >> 8), out_buff[i]);

  cras_fmt_conv_destroy(c);
}

// Test 8 to 16 bit conversion.
TEST(FormatConverterTest, ConvertU8LEToS16LE) {
  struct cras_fmt_conv *c;
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;

  size_t out_frames;
  uint8_t *in_buff;
  int16_t *out_buff;
  const size_t buf_size = 4096;

  in_fmt.format = SND_PCM_FORMAT_U8;
  out_fmt.format = SND_PCM_FORMAT_S16_LE;
  in_fmt.num_channels = 2;
  out_fmt.num_channels = 2;
  in_fmt.frame_rate = 48000;
  out_fmt.frame_rate = 48000;

  c = cras_fmt_conv_create(&in_fmt, &out_fmt, buf_size);
  ASSERT_NE(c, (void *)NULL);

  out_frames = cras_fmt_conv_in_frames_to_out(c, buf_size);
  EXPECT_EQ(buf_size, out_frames);

  in_buff = cras_fmt_conv_get_buffer(c);
  EXPECT_NE(in_buff, (void *)NULL);
  out_buff = (int16_t *)malloc(buf_size * cras_get_format_bytes(&out_fmt));
  out_frames = cras_fmt_conv_convert_to(c, (uint8_t *)out_buff, buf_size);
  EXPECT_EQ(buf_size, out_frames);
  for (unsigned int i = 0; i < buf_size; i++)
	  EXPECT_EQ(((int16_t)(in_buff[i] - 128) << 8), out_buff[i]);

  cras_fmt_conv_destroy(c);
}

// Test 32 bit 5.1 to 16 bit stereo conversion.
TEST(FormatConverterTest, ConvertS32LEToS16LEDownmix51ToStereo) {
  struct cras_fmt_conv *c;
  struct cras_audio_format in_fmt;
  struct cras_audio_format out_fmt;

  size_t out_frames;
  uint8_t *in_buff;
  int16_t *out_buff;
  const size_t buf_size = 4096;

  in_fmt.format = SND_PCM_FORMAT_S32_LE;
  out_fmt.format = SND_PCM_FORMAT_S16_LE;
  in_fmt.num_channels = 6;
  out_fmt.num_channels = 2;
  in_fmt.frame_rate = 48000;
  out_fmt.frame_rate = 48000;

  c = cras_fmt_conv_create(&in_fmt, &out_fmt, buf_size);
  ASSERT_NE(c, (void *)NULL);

  out_frames = cras_fmt_conv_in_frames_to_out(c, buf_size);
  EXPECT_EQ(buf_size, out_frames);

  in_buff = cras_fmt_conv_get_buffer(c);
  EXPECT_NE(in_buff, (void *)NULL);
  out_buff = (int16_t *)malloc(buf_size * cras_get_format_bytes(&out_fmt));
  out_frames = cras_fmt_conv_convert_to(c, (uint8_t *)out_buff, buf_size);
  EXPECT_EQ(buf_size, out_frames);

  cras_fmt_conv_destroy(c);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

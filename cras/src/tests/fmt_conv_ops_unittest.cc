// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <limits.h>
#include <math.h>
#include <memory>
#include <stdint.h>
#include <sys/param.h>

#include "cras/src/server/cras_fmt_conv_ops.h"
#include "cras_types.h"

static uint8_t* AllocateRandomBytes(size_t size) {
  uint8_t* buf = (uint8_t*)malloc(size);
  while (size--) {
    buf[size] = rand() & 0xff;
  }
  return buf;
}

using U8Ptr = std::unique_ptr<uint8_t[], decltype(free)*>;
using S16LEPtr = std::unique_ptr<int16_t[], decltype(free)*>;
using S243LEPtr = std::unique_ptr<uint8_t[], decltype(free)*>;
using S24LEPtr = std::unique_ptr<int32_t[], decltype(free)*>;
using S32LEPtr = std::unique_ptr<int32_t[], decltype(free)*>;
using FloatPtr = std::unique_ptr<float[], decltype(free)*>;

static U8Ptr CreateU8(size_t size) {
  uint8_t* buf = AllocateRandomBytes(size * sizeof(uint8_t));
  U8Ptr ret(buf, free);
  return ret;
}

static S16LEPtr CreateS16LE(size_t size) {
  uint8_t* buf = AllocateRandomBytes(size * sizeof(int16_t));
  S16LEPtr ret(reinterpret_cast<int16_t*>(buf), free);
  return ret;
}

static S243LEPtr CreateS243LE(size_t size) {
  uint8_t* buf = AllocateRandomBytes(size * sizeof(uint8_t) * 3);
  S243LEPtr ret(buf, free);
  return ret;
}

static S24LEPtr CreateS24LE(size_t size) {
  uint8_t* buf = AllocateRandomBytes(size * sizeof(int32_t));
  S24LEPtr ret(reinterpret_cast<int32_t*>(buf), free);
  return ret;
}

static S32LEPtr CreateS32LE(size_t size) {
  uint8_t* buf = AllocateRandomBytes(size * sizeof(int32_t));
  S32LEPtr ret(reinterpret_cast<int32_t*>(buf), free);
  return ret;
}

static FloatPtr CreateFloat(size_t size) {
  float* buf = (float*)malloc(size * sizeof(float));
  while (size--) {
    buf[size] = (float)(rand() & 0xff) / 0xfff;
  }
  FloatPtr ret(buf, free);
  return ret;
}

static int32_t ToS243LE(const uint8_t* in) {
  int32_t ret = 0;

  ret |= in[2];
  ret <<= 8;
  ret |= in[1];
  ret <<= 8;
  ret |= in[0];
  return ret;
}

static int16_t S16AddAndClip(int16_t a, int16_t b) {
  int32_t sum;

  sum = (int32_t)a + (int32_t)b;
  sum = MAX(sum, SHRT_MIN);
  sum = MIN(sum, SHRT_MAX);
  return sum;
}

static int32_t S32AddAndClip(int32_t a, int32_t b) {
  int64_t sum;

  sum = (int64_t)a + (int64_t)b;
  sum = MAX(sum, INT32_MIN);
  sum = MIN(sum, INT32_MAX);
  return sum;
}

// Test U8 to S16_LE conversion.
TEST(FormatConverterOpsTest, ConvertU8ToS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 2;

  U8Ptr src = CreateU8(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  convert_u8_to_s16le(src.get(), frames * in_ch, (uint8_t*)dst.get());

  for (size_t i = 0; i < frames * in_ch; ++i) {
    EXPECT_EQ((int16_t)((uint16_t)((int16_t)(int8_t)src[i] - 0x80) << 8),
              dst[i]);
  }
}

// Test S24_3LE to S16_LE conversion.
TEST(FormatConverterOpsTest, ConvertS243LEToS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 2;

  S243LEPtr src = CreateS243LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  convert_s243le_to_s16le(src.get(), frames * in_ch, (uint8_t*)dst.get());

  uint8_t* p = src.get();
  for (size_t i = 0; i < frames * in_ch; ++i) {
    EXPECT_EQ((int16_t)(ToS243LE(p) >> 8), dst[i]);
    p += 3;
  }
}

// Test S24_LE to S16_LE conversion.
TEST(FormatConverterOpsTest, ConvertS24LEToS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 2;

  S24LEPtr src = CreateS24LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  convert_s24le_to_s16le((uint8_t*)src.get(), frames * in_ch,
                         (uint8_t*)dst.get());

  for (size_t i = 0; i < frames * in_ch; ++i) {
    EXPECT_EQ((int16_t)(src[i] >> 8), dst[i]);
  }
}

// Test S32_LE to S16_LE conversion.
TEST(FormatConverterOpsTest, ConvertS32LEToS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 2;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  convert_s32le_to_s16le((uint8_t*)src.get(), frames * in_ch,
                         (uint8_t*)dst.get());

  for (size_t i = 0; i < frames * in_ch; ++i) {
    EXPECT_EQ((int16_t)(src[i] >> 16), dst[i]);
  }
}

// Test S16_LE to U8 conversion.
TEST(FormatConverterOpsTest, ConvertS16LEToU8) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 2;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  U8Ptr dst = CreateU8(frames * out_ch);

  convert_s16le_to_u8((uint8_t*)src.get(), frames * in_ch, dst.get());

  for (size_t i = 0; i < frames * in_ch; ++i) {
    EXPECT_EQ((uint8_t)(int8_t)((src[i] >> 8) + 0x80), dst[i]);
  }
}

// Test S16_LE to S24_3LE conversion.
TEST(FormatConverterOpsTest, ConvertS16LEToS243LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 2;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S243LEPtr dst = CreateS243LE(frames * out_ch);

  convert_s16le_to_s243le((uint8_t*)src.get(), frames * in_ch, dst.get());

  uint8_t* p = dst.get();
  for (size_t i = 0; i < frames * in_ch; ++i) {
    EXPECT_EQ((int32_t)((uint32_t)src[i] << 8) & 0x00ffffff,
              ToS243LE(p) & 0x00ffffff);
    p += 3;
  }
}

// Test S16_LE to S24_LE conversion.
TEST(FormatConverterOpsTest, ConvertS16LEToS24LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 2;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S24LEPtr dst = CreateS24LE(frames * out_ch);

  convert_s16le_to_s24le((uint8_t*)src.get(), frames * in_ch,
                         (uint8_t*)dst.get());

  for (size_t i = 0; i < frames * in_ch; ++i) {
    EXPECT_EQ((int32_t)((uint32_t)src[i] << 8) & 0x00ffffff,
              dst[i] & 0x00ffffff);
  }
}

// Test S16_LE to S32_LE conversion.
TEST(FormatConverterOpsTest, ConvertS16LEToS32LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 2;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  convert_s16le_to_s32le((uint8_t*)src.get(), frames * in_ch,
                         (uint8_t*)dst.get());

  for (size_t i = 0; i < frames * in_ch; ++i) {
    EXPECT_EQ((int32_t)((uint32_t)src[i] << 16) & 0xffffff00,
              dst[i] & 0xffffff00);
  }
}

TEST(FormatConverterOpsTest, ConvertF32LEToS16LE) {
  constexpr size_t frames = 7;
  float src[frames] = {-2.f, -1.f, -0.5f, 0.f, 0.5f, 1.f, 2.f};
  int16_t dst[frames] = {0};
  int16_t expected[frames] = {INT16_MIN, INT16_MIN, -32768 / 2, 0,
                              32768 / 2, INT16_MAX, INT16_MAX};

  convert_f32le_to_s16le(src, frames, dst);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(dst[i], expected[i]);
  }
}

TEST(FormatConverterOpsTest, ConvertS16LEToF32LE) {
  constexpr size_t frames = 5;
  int16_t src[frames] = {INT16_MIN, -32768 / 2, 0, 32768 / 2, INT16_MAX};
  float dst[frames] = {0};
  float expected[frames] = {-1.f, -0.5f, 0.f, 0.5f, INT16_MAX / 32768.f};

  convert_s16le_to_f32le(src, frames, dst);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(dst[i], expected[i]);
  }
}

// Test Mono to Stereo conversion.  S16_LE.
TEST(FormatConverterOpsTest, MonoToStereoS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 2;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret =
      s16_mono_to_stereo((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i], dst[i * 2 + 0]);
    EXPECT_EQ(src[i], dst[i * 2 + 1]);
  }
}

// Test Stereo to Mono conversion.  S16_LE.
TEST(FormatConverterOpsTest, StereoToMonoS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);
  for (size_t i = 0; i < frames; ++i) {
    src[i * 2 + 0] = 13450;
    src[i * 2 + 1] = -13449;
  }

  size_t ret =
      s16_stereo_to_mono((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(1, dst[i]);
  }
}

// Test Stereo to Mono conversion.  S16_LE, Overflow.
TEST(FormatConverterOpsTest, StereoToMonoS16LEOverflow) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);
  for (size_t i = 0; i < frames; ++i) {
    src[i * 2 + 0] = 0x7fff;
    src[i * 2 + 1] = 1;
  }

  size_t ret =
      s16_stereo_to_mono((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(0x7fff, dst[i]);
  }
}

// Test Stereo to Mono conversion.  S16_LE, Underflow.
TEST(FormatConverterOpsTest, StereoToMonoS16LEUnderflow) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);
  for (size_t i = 0; i < frames; ++i) {
    src[i * 2 + 0] = -0x8000;
    src[i * 2 + 1] = -0x1;
  }

  size_t ret =
      s16_stereo_to_mono((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(-0x8000, dst[i]);
  }
}

// Test Mono to 5.1 conversion.  S16_LE, Center.
TEST(FormatConverterOpsTest, MonoTo51S16LECenter) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 6;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = 4;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_mono_to_51(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == center) {
        EXPECT_EQ(src[i], dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 6 + k]);
      }
    }
  }
}

// Test Mono to 5.1 conversion.  S16_LE, LeftRight.
TEST(FormatConverterOpsTest, MonoTo51S16LELeftRight) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 6;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = -1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_mono_to_51(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == left) {
        EXPECT_EQ(src[i] / 2, dst[i * 6 + k]);
      } else if (k == right) {
        EXPECT_EQ(src[i] / 2, dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 6 + k]);
      }
    }
  }
}

// Test Mono to 5.1 conversion.  S16_LE, Unknown.
TEST(FormatConverterOpsTest, MonoTo51S16LEUnknown) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 6;
  const size_t left = -1;
  const size_t right = -1;
  const size_t center = -1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_mono_to_51(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == 0) {
        EXPECT_EQ(src[i], dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[6 * i + k]);
      }
    }
  }
}

// Test Stereo to 5.1 conversion.  S16_LE, Center.
TEST(FormatConverterOpsTest, StereoTo51S16LECenter) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 6;
  const size_t left = -1;
  const size_t right = 1;
  const size_t center = 4;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_stereo_to_51(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == center) {
        EXPECT_EQ(S16AddAndClip(src[i * 2], src[i * 2 + 1]), dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 6 + k]);
      }
    }
  }
}

// Test Quad to 5.1 conversion. S16_LE.
TEST(FormatConverterOpsTest, QuadTo51S16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 4;
  const size_t out_ch = 6;
  const unsigned int fl_quad = 0;
  const unsigned int fr_quad = 1;
  const unsigned int rl_quad = 2;
  const unsigned int rr_quad = 3;

  const unsigned int fl_51 = 0;
  const unsigned int fr_51 = 1;
  const unsigned int center_51 = 2;
  const unsigned int lfe_51 = 3;
  const unsigned int rl_51 = 4;
  const unsigned int rr_51 = 5;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_quad_to_51(fl_51, fr_51, rl_51, rr_51, (uint8_t*)src.get(),
                              frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);
  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(0, dst[i * 6 + center_51]);
    EXPECT_EQ(0, dst[i * 6 + lfe_51]);
    EXPECT_EQ(src[i * 4 + fl_quad], dst[i * 6 + fl_51]);
    EXPECT_EQ(src[i * 4 + fr_quad], dst[i * 6 + fr_51]);
    EXPECT_EQ(src[i * 4 + rl_quad], dst[i * 6 + rl_51]);
    EXPECT_EQ(src[i * 4 + rr_quad], dst[i * 6 + rr_51]);
  }
}

// Test Stereo to 5.1 conversion.  S16_LE, LeftRight.
TEST(FormatConverterOpsTest, StereoTo51S16LELeftRight) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 6;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = -1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_stereo_to_51(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == left) {
        EXPECT_EQ(src[i * 2 + 0], dst[i * 6 + k]);
      } else if (k == right) {
        EXPECT_EQ(src[i * 2 + 1], dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 6 + k]);
      }
    }
  }
}

// Test Stereo to 5.1 conversion.  S16_LE, Unknown.
TEST(FormatConverterOpsTest, StereoTo51S16LEUnknown) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 6;
  const size_t left = -1;
  const size_t right = -1;
  const size_t center = -1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_stereo_to_51(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == 0 || k == 1) {
        EXPECT_EQ(src[i * 2 + k], dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 6 + k]);
      }
    }
  }
}

// Test 5.1 to Stereo conversion.  S16_LE.
TEST(FormatConverterOpsTest, _51ToStereoS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 6;
  const size_t out_ch = 2;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = 2;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret =
      s16_51_to_stereo((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  /* Use the normalized_factor from the left channel = 1 / (|1| + |0.707|)
   * to prevent mixing overflow.
   */
  const float normalized_factor = 0.585;

  for (size_t i = 0; i < frames; ++i) {
    int16_t half_center = src[i * 6 + center] * 0.707 * normalized_factor;
    int16_t l = normalized_factor * src[i * 6 + left] + half_center;
    int16_t r = normalized_factor * src[i * 6 + right] + half_center;

    EXPECT_EQ(l, dst[i * 2 + left]);
    EXPECT_EQ(r, dst[i * 2 + right]);
  }
}

// Test 5.1 to Quad conversion.  S16_LE.
TEST(FormatConverterOpsTest, _51ToQuadS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 6;
  const size_t out_ch = 4;
  const unsigned int fl_quad = 0;
  const unsigned int fr_quad = 1;
  const unsigned int rl_quad = 2;
  const unsigned int rr_quad = 3;

  const unsigned int fl_51 = 0;
  const unsigned int fr_51 = 1;
  const unsigned int center_51 = 2;
  const unsigned int lfe_51 = 3;
  const unsigned int rl_51 = 4;
  const unsigned int rr_51 = 5;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_51_to_quad((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  /* Use normalized_factor from the left channel = 1 / (|1| + |0.707| + |0.5|)
   * to prevent overflow. */
  const float normalized_factor = 0.453;
  for (size_t i = 0; i < frames; ++i) {
    int16_t half_center = src[i * 6 + center_51] * 0.707 * normalized_factor;
    int16_t lfe = src[6 * i + lfe_51] * 0.5 * normalized_factor;
    int16_t fl = normalized_factor * src[6 * i + fl_51] + half_center + lfe;
    int16_t fr = normalized_factor * src[6 * i + fr_51] + half_center + lfe;
    int16_t rl = normalized_factor * src[6 * i + rl_51] + lfe;
    int16_t rr = normalized_factor * src[6 * i + rr_51] + lfe;
    EXPECT_EQ(fl, dst[4 * i + fl_quad]);
    EXPECT_EQ(fr, dst[4 * i + fr_quad]);
    EXPECT_EQ(rl, dst[4 * i + rl_quad]);
    EXPECT_EQ(rr, dst[4 * i + rr_quad]);
  }
}

// Test Stereo to Quad conversion.  S16_LE, Specify.
TEST(FormatConverterOpsTest, StereoToQuadS16LESpecify) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 4;
  const size_t front_left = 2;
  const size_t front_right = 3;
  const size_t rear_left = 0;
  const size_t rear_right = 1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_stereo_to_quad(front_left, front_right, (uint8_t*)src.get(),
                                  frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 2 + 0], dst[i * 4 + front_left]);
    EXPECT_EQ(0, dst[i * 4 + rear_left]);
    EXPECT_EQ(src[i * 2 + 1], dst[i * 4 + front_right]);
    EXPECT_EQ(0, dst[i * 4 + rear_right]);
  }
}

// Test Stereo to Quad conversion.  S16_LE, Default.
TEST(FormatConverterOpsTest, StereoToQuadS16LEDefault) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 4;
  const size_t front_left = -1;
  const size_t front_right = -1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_stereo_to_quad(front_left, front_right, (uint8_t*)src.get(),
                                  frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 2 + 0], dst[i * 4 + 0]);
    EXPECT_EQ(0, dst[i * 4 + 2]);
    EXPECT_EQ(src[i * 2 + 1], dst[i * 4 + 1]);
    EXPECT_EQ(0, dst[i * 4 + 3]);
  }
}

// Test Quad to Stereo conversion.  S16_LE, Specify.
TEST(FormatConverterOpsTest, QuadToStereoS16LESpecify) {
  const size_t frames = 4096;
  const size_t in_ch = 4;
  const size_t out_ch = 2;
  const size_t front_left = 2;
  const size_t front_right = 3;
  const size_t rear_left = 0;
  const size_t rear_right = 1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret =
      s16_quad_to_stereo(front_left, front_right, rear_left, rear_right,
                         (uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    int16_t left =
        S16AddAndClip(src[i * 4 + front_left], src[i * 4 + rear_left] / 4);
    int16_t right =
        S16AddAndClip(src[i * 4 + front_right], src[i * 4 + rear_right] / 4);
    EXPECT_EQ(left, dst[i * 2 + 0]);
    EXPECT_EQ(right, dst[i * 2 + 1]);
  }
}

// Test Quad to Stereo conversion.  S16_LE, Default.
TEST(FormatConverterOpsTest, QuadToStereoS16LEDefault) {
  const size_t frames = 4096;
  const size_t in_ch = 4;
  const size_t out_ch = 2;
  const size_t front_left = -1;
  const size_t front_right = -1;
  const size_t rear_left = -1;
  const size_t rear_right = -1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret =
      s16_quad_to_stereo(front_left, front_right, rear_left, rear_right,
                         (uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    int16_t left = S16AddAndClip(src[i * 4 + 0], src[i * 4 + 2] / 4);
    int16_t right = S16AddAndClip(src[i * 4 + 1], src[i * 4 + 3] / 4);
    EXPECT_EQ(left, dst[i * 2 + 0]);
    EXPECT_EQ(right, dst[i * 2 + 1]);
  }
}

// Test mono to 8ch conversion.  S16_LE, Center.
TEST(FormatConverterOpsTest, MonoTo8chS16LECenter) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 8;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = 2;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_mono_to_71(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == center) {
        EXPECT_EQ(src[i], dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test mono to 8ch conversion.  S16_LE, LeftRight.
TEST(FormatConverterOpsTest, MonoTo8chS16LELeftRight) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 8;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = -1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_mono_to_71(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == left || k == right) {
        EXPECT_EQ(src[i] / 2, dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test mono to 8ch conversion.  S16_LE, Unknown.
TEST(FormatConverterOpsTest, MonoTo8chS16LEUnknown) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 8;
  const size_t left = -1;
  const size_t right = -1;
  const size_t center = -1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_mono_to_71(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == 0) {
        EXPECT_EQ(src[i], dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test stereo to 8ch conversion.  S16_LE, LeftRight.
TEST(FormatConverterOpsTest, StereoTo8chS16LELeftRight) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 8;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = -1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_stereo_to_71(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == left) {
        EXPECT_EQ(src[i * 2], dst[i * 8 + k]);
      } else if (k == right) {
        EXPECT_EQ(src[i * 2 + 1], dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test stereo to 8ch conversion.  S16_LE, Center.
TEST(FormatConverterOpsTest, StereoTo8chS16LECenter) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 8;
  const size_t left = -1;
  const size_t right = -1;
  const size_t center = 2;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_stereo_to_71(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == center) {
        EXPECT_EQ(S16AddAndClip(src[i * 2], src[i * 2 + 1]), dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test stereo to 8ch conversion.  S16_LE, Unknown.
TEST(FormatConverterOpsTest, StereoTo8chS16LEUnknown) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 8;
  const size_t left = -1;
  const size_t right = -1;
  const size_t center = -1;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_stereo_to_71(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == 0 || k == 1) {
        EXPECT_EQ(src[i * 2 + k], dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test quad to 8ch conversion.  S16_LE, Specify.
TEST(FormatConverterOpsTest, QuadTo8chS16LESpecify) {
  const size_t frames = 4096;
  const size_t in_ch = 4;
  const size_t out_ch = 8;

  const size_t fl_quad = 0;
  const size_t fr_quad = 1;
  const size_t rl_quad = 2;
  const size_t rr_quad = 3;

  // Specify custom channel mapping
  const size_t fl_71 = 7;
  const size_t fr_71 = 6;
  const size_t center_71 = 5;
  const size_t lfe_71 = 4;
  const size_t rl_71 = 3;
  const size_t rr_71 = 2;
  const size_t sl_71 = 1;
  const size_t sr_71 = 0;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_quad_to_71(fl_71, fr_71, rl_71, rr_71, (uint8_t*)src.get(),
                              frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 4 + fl_quad], dst[i * 8 + fl_71]);
    EXPECT_EQ(src[i * 4 + fr_quad], dst[i * 8 + fr_71]);
    EXPECT_EQ(src[i * 4 + rl_quad], dst[i * 8 + rl_71]);
    EXPECT_EQ(src[i * 4 + rr_quad], dst[i * 8 + rr_71]);

    EXPECT_EQ(0, dst[i * 8 + center_71]);
    EXPECT_EQ(0, dst[i * 8 + lfe_71]);
    EXPECT_EQ(0, dst[i * 8 + sl_71]);
    EXPECT_EQ(0, dst[i * 8 + sr_71]);
  }
}

// Test quad to 8ch conversion.  S16_LE, Default.
TEST(FormatConverterOpsTest, QuadTo8chS16LEDefault) {
  const size_t frames = 4096;
  const size_t in_ch = 4;
  const size_t out_ch = 8;

  const size_t fl_quad = 0;
  const size_t fr_quad = 1;
  const size_t rl_quad = 2;
  const size_t rr_quad = 3;

  const size_t fl_71 = 0;
  const size_t fr_71 = 1;
  const size_t center_71 = 2;
  const size_t lfe_71 = 3;
  const size_t rl_71 = 4;
  const size_t rr_71 = 5;
  const size_t sl_71 = 6;
  const size_t sr_71 = 7;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_quad_to_71(-1, -1, -1, -1, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 4 + fl_quad], dst[i * 8 + fl_71]);
    EXPECT_EQ(src[i * 4 + fr_quad], dst[i * 8 + fr_71]);
    EXPECT_EQ(src[i * 4 + rl_quad], dst[i * 8 + rl_71]);
    EXPECT_EQ(src[i * 4 + rr_quad], dst[i * 8 + rr_71]);

    EXPECT_EQ(0, dst[i * 8 + center_71]);
    EXPECT_EQ(0, dst[i * 8 + lfe_71]);
    EXPECT_EQ(0, dst[i * 8 + sl_71]);
    EXPECT_EQ(0, dst[i * 8 + sr_71]);
  }
}

// Test 6ch to 8ch conversion.  S16_LE, Specify, Rear.
TEST(FormatConverterOpsTest, 6chTo8chS16LESpecifyRear) {
  const size_t frames = 4096;
  const size_t in_ch = 6;
  const size_t out_ch = 8;

  // FL FR FC LFE RL RR
  const struct cras_audio_format in_fmt = {
      .channel_layout = {0, 1, 4, 5, 2, 3, -1, -1, -1, -1, -1},
  };
  const struct cras_audio_format out_fmt = {
      .channel_layout = {0, 1, 2, 3, 4, 5, 6, 7, -1, -1, -1},
  };

  const size_t fl_51 = in_fmt.channel_layout[CRAS_CH_FL];
  const size_t fr_51 = in_fmt.channel_layout[CRAS_CH_FR];
  const size_t center_51 = in_fmt.channel_layout[CRAS_CH_FC];
  const size_t lfe_51 = in_fmt.channel_layout[CRAS_CH_LFE];
  const size_t rl_51 = in_fmt.channel_layout[CRAS_CH_RL];
  const size_t rr_51 = in_fmt.channel_layout[CRAS_CH_RR];

  // Specify custom channel mapping
  const size_t fl_71 = out_fmt.channel_layout[CRAS_CH_FL];
  const size_t fr_71 = out_fmt.channel_layout[CRAS_CH_FR];
  const size_t center_71 = out_fmt.channel_layout[CRAS_CH_FC];
  const size_t lfe_71 = out_fmt.channel_layout[CRAS_CH_LFE];
  const size_t rl_71 = out_fmt.channel_layout[CRAS_CH_RL];
  const size_t rr_71 = out_fmt.channel_layout[CRAS_CH_RR];
  const size_t sl_71 = out_fmt.channel_layout[CRAS_CH_SL];
  const size_t sr_71 = out_fmt.channel_layout[CRAS_CH_SR];

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_51_to_71(&in_fmt, &out_fmt, (uint8_t*)src.get(), frames,
                            (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 6 + fl_51], dst[i * 8 + fl_71]);
    EXPECT_EQ(src[i * 6 + fr_51], dst[i * 8 + fr_71]);
    EXPECT_EQ(src[i * 6 + center_51], dst[i * 8 + center_71]);
    EXPECT_EQ(src[i * 6 + lfe_51], dst[i * 8 + lfe_71]);
    EXPECT_EQ(src[i * 6 + rl_51], dst[i * 8 + rl_71]);
    EXPECT_EQ(src[i * 6 + rr_51], dst[i * 8 + rr_71]);

    EXPECT_EQ(0, dst[i * 8 + sl_71]);
    EXPECT_EQ(0, dst[i * 8 + sr_71]);
  }
}

// Test 6ch to 8ch conversion.  S16_LE, Specify, Side.
TEST(FormatConverterOpsTest, 6chTo8chS16LESpecifySide) {
  const size_t frames = 4096;
  const size_t in_ch = 6;
  const size_t out_ch = 8;

  // FL FR FC LFE SL SR
  const struct cras_audio_format in_fmt = {
      .channel_layout = {0, 1, -1, -1, 2, 3, 4, 5, -1, -1, -1},
  };
  const struct cras_audio_format out_fmt = {
      .channel_layout = {0, 1, 2, 3, 4, 5, 6, 7, -1, -1, -1},
  };

  const size_t fl_51 = in_fmt.channel_layout[CRAS_CH_FL];
  const size_t fr_51 = in_fmt.channel_layout[CRAS_CH_FR];
  const size_t center_51 = in_fmt.channel_layout[CRAS_CH_FC];
  const size_t lfe_51 = in_fmt.channel_layout[CRAS_CH_LFE];
  const size_t sl_51 = in_fmt.channel_layout[CRAS_CH_SL];
  const size_t sr_51 = in_fmt.channel_layout[CRAS_CH_SR];

  // Specify custom channel mapping
  const size_t fl_71 = out_fmt.channel_layout[CRAS_CH_FL];
  const size_t fr_71 = out_fmt.channel_layout[CRAS_CH_FR];
  const size_t center_71 = out_fmt.channel_layout[CRAS_CH_FC];
  const size_t lfe_71 = out_fmt.channel_layout[CRAS_CH_LFE];
  const size_t rl_71 = out_fmt.channel_layout[CRAS_CH_RL];
  const size_t rr_71 = out_fmt.channel_layout[CRAS_CH_RR];
  const size_t sl_71 = out_fmt.channel_layout[CRAS_CH_SL];
  const size_t sr_71 = out_fmt.channel_layout[CRAS_CH_SR];

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_51_to_71(&in_fmt, &out_fmt, (uint8_t*)src.get(), frames,
                            (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 6 + fl_51], dst[i * 8 + fl_71]);
    EXPECT_EQ(src[i * 6 + fr_51], dst[i * 8 + fr_71]);
    EXPECT_EQ(src[i * 6 + center_51], dst[i * 8 + center_71]);
    EXPECT_EQ(src[i * 6 + lfe_51], dst[i * 8 + lfe_71]);
    EXPECT_EQ(src[i * 6 + sl_51], dst[i * 8 + sl_71]);
    EXPECT_EQ(src[i * 6 + sr_51], dst[i * 8 + sr_71]);

    EXPECT_EQ(0, dst[i * 8 + rl_71]);
    EXPECT_EQ(0, dst[i * 8 + rr_71]);
  }
}

// Test 6ch to 8ch conversion.  S16_LE, Default.
TEST(FormatConverterOpsTest, 6chTo8chS16LEDefault) {
  const size_t frames = 4096;
  const size_t in_ch = 6;
  const size_t out_ch = 8;

  const struct cras_audio_format in_fmt = {
      .channel_layout = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
  };
  const struct cras_audio_format out_fmt = {
      .channel_layout = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
  };

  const size_t fl_51 = 0;
  const size_t fr_51 = 1;
  const size_t center_51 = 2;
  const size_t lfe_51 = 3;
  const size_t rl_51 = 4;
  const size_t rr_51 = 5;

  const size_t fl_71 = 0;
  const size_t fr_71 = 1;
  const size_t center_71 = 2;
  const size_t lfe_71 = 3;
  const size_t rl_71 = 4;
  const size_t rr_71 = 5;
  const size_t sl_71 = 6;
  const size_t sr_71 = 7;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_51_to_71(&in_fmt, &out_fmt, (uint8_t*)src.get(), frames,
                            (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 6 + fl_51], dst[i * 8 + fl_71]);
    EXPECT_EQ(src[i * 6 + fr_51], dst[i * 8 + fr_71]);
    EXPECT_EQ(src[i * 6 + center_51], dst[i * 8 + center_71]);
    EXPECT_EQ(src[i * 6 + lfe_51], dst[i * 8 + lfe_71]);
    EXPECT_EQ(src[i * 6 + rl_51], dst[i * 8 + rl_71]);
    EXPECT_EQ(src[i * 6 + rr_51], dst[i * 8 + rr_71]);

    EXPECT_EQ(0, dst[i * 8 + sl_71]);
    EXPECT_EQ(0, dst[i * 8 + sr_71]);
  }
}

// Test Stereo to 3ch conversion.  S16_LE.
TEST(FormatConverterOpsTest, StereoTo3chS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 3;
  struct cras_audio_format fmt = {
      .format = SND_PCM_FORMAT_S16_LE,
      .frame_rate = 48000,
      .num_channels = 3,
  };

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_default_all_to_all(&fmt, in_ch, out_ch, (uint8_t*)src.get(),
                                      frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    int32_t sum = 0;
    for (size_t k = 0; k < in_ch; ++k) {
      sum += (int32_t)src[i * in_ch + k];
    }
    src[i * in_ch + 0] = (int16_t)(sum / (int32_t)in_ch);
  }
  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < out_ch; ++k) {
      EXPECT_EQ(src[i * in_ch + 0], dst[i * out_ch + k]);
    }
  }
}

// Test 6ch to 8ch conversion with all_to_all.  S16_LE.
TEST(FormatConverterOpsTest, 6chTo8chAllToAllS16LE) {
  const size_t frames = 65536;
  const size_t in_ch = 6;
  const size_t out_ch = 8;
  struct cras_audio_format fmt = {
      .format = SND_PCM_FORMAT_S16_LE,
      .frame_rate = 48000,
      .num_channels = 8,
  };

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);
  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < in_ch; k++) {
      src[i * in_ch + k] = (k == 0) ? (INT16_MIN + (int16_t)i) : 0;
    }
  }

  size_t ret = s16_default_all_to_all(&fmt, in_ch, out_ch, (uint8_t*)src.get(),
                                      frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    src[i * in_ch + 0] /= (int16_t)in_ch;
    for (size_t k = 0; k < out_ch; ++k) {
      EXPECT_EQ(src[i * in_ch + 0], dst[i * out_ch + k]);
    }
  }
}

// Test Multiply with Coef.  S16_LE.
TEST(FormatConverterOpsTest, MultiplyWithCoefS16LE) {
  const size_t buf_size = 4096;

  S16LEPtr buf = CreateS16LE(buf_size);
  FloatPtr coef = CreateFloat(buf_size);

  int16_t ret = s16_multiply_buf_with_coef(coef.get(), buf.get(), buf_size);

  int32_t exp = 0;
  for (size_t i = 0; i < buf_size; ++i) {
    exp += coef[i] * buf[i];
  }
  exp = MIN(MAX(exp, SHRT_MIN), SHRT_MAX);

  EXPECT_EQ((int16_t)exp, ret);
}

// Test Convert Channels.  S16_LE.
TEST(FormatConverterOpsTest, ConvertChannelsS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 3;

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);
  FloatPtr ch_conv_mtx = CreateFloat(out_ch * in_ch);
  std::unique_ptr<float*[]> mtx(new float*[out_ch]);
  for (size_t i = 0; i < out_ch; ++i) {
    mtx[i] = &ch_conv_mtx[i * in_ch];
  }

  size_t ret =
      s16_convert_channels(mtx.get(), in_ch, out_ch, (uint8_t*)src.get(),
                           frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t fr = 0; fr < frames; ++fr) {
    for (size_t i = 0; i < out_ch; ++i) {
      int16_t exp = 0;
      for (size_t k = 0; k < in_ch; ++k) {
        exp += mtx[i][k] * src[fr * in_ch + k];
      }
      exp = MIN(MAX(exp, SHRT_MIN), SHRT_MAX);
      EXPECT_EQ(exp, dst[fr * out_ch + i]);
    }
  }
}

// Test Stereo to 20ch conversion.  S16_LE.
TEST(FormatConverterOpsTest, TwoToTwentyS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 20;
  struct cras_audio_format fmt = {
      .format = SND_PCM_FORMAT_S16_LE,
      .frame_rate = 48000,
      .num_channels = 20,
  };

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_some_to_some(&fmt, in_ch, out_ch, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    size_t k;
    // Input channels should be directly copied over.
    for (k = 0; k < in_ch; ++k) {
      EXPECT_EQ(src[i * in_ch + k], dst[i * out_ch + k]);
    }
    // The rest should be zeroed.
    for (; k < out_ch; ++k) {
      EXPECT_EQ(0, dst[i * out_ch + k]);
    }
  }
}

// Test 20ch to Stereo.  S16_LE.
TEST(FormatConverterOpsTest, TwentyToTwoS16LE) {
  const size_t frames = 4096;
  const size_t in_ch = 20;
  const size_t out_ch = 2;
  struct cras_audio_format fmt = {
      .format = SND_PCM_FORMAT_S16_LE,
      .frame_rate = 48000,
      .num_channels = 2,
  };

  S16LEPtr src = CreateS16LE(frames * in_ch);
  S16LEPtr dst = CreateS16LE(frames * out_ch);

  size_t ret = s16_some_to_some(&fmt, in_ch, out_ch, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    size_t k;
    // Input channels should be directly copied over.
    for (k = 0; k < out_ch; ++k) {
      EXPECT_EQ(src[i * in_ch + k], dst[i * out_ch + k]);
    }
  }
}

// Test Mono to Stereo conversion.  S32_LE.
TEST(FormatConverterOpsTest, MonoToStereoS32LE) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 2;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret =
      s32_mono_to_stereo((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i], dst[i * 2 + 0]);
    EXPECT_EQ(src[i], dst[i * 2 + 1]);
  }
}

// Test Stereo to Mono conversion.  S32_LE.
TEST(FormatConverterOpsTest, StereoToMonoS32LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);
  for (size_t i = 0; i < frames; ++i) {
    src[i * 2 + 0] = 13450;
    src[i * 2 + 1] = -13449;
  }

  size_t ret =
      s32_stereo_to_mono((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(1, dst[i]);
  }
}

// Test Stereo to Mono conversion.  S32_LE, Overflow.
TEST(FormatConverterOpsTest, StereoToMonoS32LEOverflow) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);
  for (size_t i = 0; i < frames; ++i) {
    src[i * 2 + 0] = INT32_MAX;
    src[i * 2 + 1] = 1;
  }

  size_t ret =
      s32_stereo_to_mono((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(INT32_MAX, dst[i]);
  }
}

// Test Stereo to Mono conversion.  S32_LE, Underflow.
TEST(FormatConverterOpsTest, StereoToMonoS32LEUnderflow) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);
  for (size_t i = 0; i < frames; ++i) {
    src[i * 2 + 0] = INT32_MIN;
    src[i * 2 + 1] = -0x1;
  }

  size_t ret =
      s32_stereo_to_mono((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(INT32_MIN, dst[i]);
  }
}

// Test Mono to 5.1 conversion.  S32_LE, Center.
TEST(FormatConverterOpsTest, MonoTo51S32LECenter) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 6;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = 4;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_mono_to_51(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == center) {
        EXPECT_EQ(src[i], dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 6 + k]);
      }
    }
  }
}

// Test Mono to 5.1 conversion.  S32_LE, LeftRight.
TEST(FormatConverterOpsTest, MonoTo51S32LELeftRight) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 6;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = -1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_mono_to_51(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == left) {
        EXPECT_EQ(src[i] / 2, dst[i * 6 + k]);
      } else if (k == right) {
        EXPECT_EQ(src[i] / 2, dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 6 + k]);
      }
    }
  }
}

// Test Mono to 5.1 conversion.  S32_LE, Unknown.
TEST(FormatConverterOpsTest, MonoTo51S32LEUnknown) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 6;
  const size_t left = -1;
  const size_t right = -1;
  const size_t center = -1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_mono_to_51(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == 0) {
        EXPECT_EQ(src[i], dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[6 * i + k]);
      }
    }
  }
}

// Test Stereo to 5.1 conversion.  S32_LE, Center.
TEST(FormatConverterOpsTest, StereoTo51S32LECenter) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 6;
  const size_t left = -1;
  const size_t right = 1;
  const size_t center = 4;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_stereo_to_51(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == center) {
        EXPECT_EQ(S32AddAndClip(src[i * 2], src[i * 2 + 1]), dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 6 + k]);
      }
    }
  }
}

// Test Quad to 5.1 conversion. S32_LE.
TEST(FormatConverterOpsTest, QuadTo51S32LE) {
  const size_t frames = 4096;
  const size_t in_ch = 4;
  const size_t out_ch = 6;
  const unsigned int fl_quad = 0;
  const unsigned int fr_quad = 1;
  const unsigned int rl_quad = 2;
  const unsigned int rr_quad = 3;

  const unsigned int fl_51 = 0;
  const unsigned int fr_51 = 1;
  const unsigned int center_51 = 2;
  const unsigned int lfe_51 = 3;
  const unsigned int rl_51 = 4;
  const unsigned int rr_51 = 5;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_quad_to_51(fl_51, fr_51, rl_51, rr_51, (uint8_t*)src.get(),
                              frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);
  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(0, dst[i * 6 + center_51]);
    EXPECT_EQ(0, dst[i * 6 + lfe_51]);
    EXPECT_EQ(src[i * 4 + fl_quad], dst[i * 6 + fl_51]);
    EXPECT_EQ(src[i * 4 + fr_quad], dst[i * 6 + fr_51]);
    EXPECT_EQ(src[i * 4 + rl_quad], dst[i * 6 + rl_51]);
    EXPECT_EQ(src[i * 4 + rr_quad], dst[i * 6 + rr_51]);
  }
}

// Test Stereo to 5.1 conversion.  S32_LE, LeftRight.
TEST(FormatConverterOpsTest, StereoTo51S32LELeftRight) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 6;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = -1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_stereo_to_51(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == left) {
        EXPECT_EQ(src[i * 2 + 0], dst[i * 6 + k]);
      } else if (k == right) {
        EXPECT_EQ(src[i * 2 + 1], dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 6 + k]);
      }
    }
  }
}

// Test Stereo to 5.1 conversion.  S32_LE, Unknown.
TEST(FormatConverterOpsTest, StereoTo51S32LEUnknown) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 6;
  const size_t left = -1;
  const size_t right = -1;
  const size_t center = -1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_stereo_to_51(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 6; ++k) {
      if (k == 0 || k == 1) {
        EXPECT_EQ(src[i * 2 + k], dst[i * 6 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 6 + k]);
      }
    }
  }
}

// Test 5.1 to Stereo conversion.  S32_LE.
TEST(FormatConverterOpsTest, _51ToStereoS32LE) {
  const size_t frames = 4096;
  const size_t in_ch = 6;
  const size_t out_ch = 2;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = 2;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret =
      s32_51_to_stereo((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  /* Use the normalized_factor from the left channel = 1 / (|1| + |0.707|)
   * to prevent mixing overflow.
   */
  const float normalized_factor = 0.585;

  for (size_t i = 0; i < frames; ++i) {
    int32_t half_center = src[i * 6 + center] * 0.707 * normalized_factor;
    int32_t l = normalized_factor * src[i * 6 + left] + half_center;
    int32_t r = normalized_factor * src[i * 6 + right] + half_center;

    EXPECT_EQ(l, dst[i * 2 + left]);
    EXPECT_EQ(r, dst[i * 2 + right]);
  }
}

// Test 5.1 to Quad conversion.  S32_LE.
TEST(FormatConverterOpsTest, _51ToQuadS32LE) {
  const size_t frames = 4096;
  const size_t in_ch = 6;
  const size_t out_ch = 4;
  const unsigned int fl_quad = 0;
  const unsigned int fr_quad = 1;
  const unsigned int rl_quad = 2;
  const unsigned int rr_quad = 3;

  const unsigned int fl_51 = 0;
  const unsigned int fr_51 = 1;
  const unsigned int center_51 = 2;
  const unsigned int lfe_51 = 3;
  const unsigned int rl_51 = 4;
  const unsigned int rr_51 = 5;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_51_to_quad((uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  /* Use normalized_factor from the left channel = 1 / (|1| + |0.707| + |0.5|)
   * to prevent overflow. */
  const float normalized_factor = 0.453;
  for (size_t i = 0; i < frames; ++i) {
    int32_t half_center = src[i * 6 + center_51] * 0.707 * normalized_factor;
    int32_t lfe = src[6 * i + lfe_51] * 0.5 * normalized_factor;
    int32_t fl = normalized_factor * src[6 * i + fl_51] + half_center + lfe;
    int32_t fr = normalized_factor * src[6 * i + fr_51] + half_center + lfe;
    int32_t rl = normalized_factor * src[6 * i + rl_51] + lfe;
    int32_t rr = normalized_factor * src[6 * i + rr_51] + lfe;
    EXPECT_EQ(fl, dst[4 * i + fl_quad]);
    EXPECT_EQ(fr, dst[4 * i + fr_quad]);
    EXPECT_EQ(rl, dst[4 * i + rl_quad]);
    EXPECT_EQ(rr, dst[4 * i + rr_quad]);
  }
}

// Test Stereo to Quad conversion.  S32_LE, Specify.
TEST(FormatConverterOpsTest, StereoToQuadS32LESpecify) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 4;
  const size_t front_left = 2;
  const size_t front_right = 3;
  const size_t rear_left = 0;
  const size_t rear_right = 1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_stereo_to_quad(front_left, front_right, (uint8_t*)src.get(),
                                  frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 2 + 0], dst[i * 4 + front_left]);
    EXPECT_EQ(0, dst[i * 4 + rear_left]);
    EXPECT_EQ(src[i * 2 + 1], dst[i * 4 + front_right]);
    EXPECT_EQ(0, dst[i * 4 + rear_right]);
  }
}

// Test Stereo to Quad conversion.  S32_LE, Default.
TEST(FormatConverterOpsTest, StereoToQuadS32LEDefault) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 4;
  const size_t front_left = -1;
  const size_t front_right = -1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_stereo_to_quad(front_left, front_right, (uint8_t*)src.get(),
                                  frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 2 + 0], dst[i * 4 + 0]);
    EXPECT_EQ(0, dst[i * 4 + 2]);
    EXPECT_EQ(src[i * 2 + 1], dst[i * 4 + 1]);
    EXPECT_EQ(0, dst[i * 4 + 3]);
  }
}

// Test Quad to Stereo conversion.  S32_LE, Specify.
TEST(FormatConverterOpsTest, QuadToStereoS32LESpecify) {
  const size_t frames = 4096;
  const size_t in_ch = 4;
  const size_t out_ch = 2;
  const size_t front_left = 2;
  const size_t front_right = 3;
  const size_t rear_left = 0;
  const size_t rear_right = 1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret =
      s32_quad_to_stereo(front_left, front_right, rear_left, rear_right,
                         (uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    int32_t left =
        S32AddAndClip(src[i * 4 + front_left], src[i * 4 + rear_left] / 4);
    int32_t right =
        S32AddAndClip(src[i * 4 + front_right], src[i * 4 + rear_right] / 4);
    EXPECT_EQ(left, dst[i * 2 + 0]);
    EXPECT_EQ(right, dst[i * 2 + 1]);
  }
}

// Test Quad to Stereo conversion.  S32_LE, Default.
TEST(FormatConverterOpsTest, QuadToStereoS32LEDefault) {
  const size_t frames = 4096;
  const size_t in_ch = 4;
  const size_t out_ch = 2;
  const size_t front_left = -1;
  const size_t front_right = -1;
  const size_t rear_left = -1;
  const size_t rear_right = -1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret =
      s32_quad_to_stereo(front_left, front_right, rear_left, rear_right,
                         (uint8_t*)src.get(), frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    int32_t left = S32AddAndClip(src[i * 4 + 0], src[i * 4 + 2] / 4);
    int32_t right = S32AddAndClip(src[i * 4 + 1], src[i * 4 + 3] / 4);
    EXPECT_EQ(left, dst[i * 2 + 0]);
    EXPECT_EQ(right, dst[i * 2 + 1]);
  }
}

// Test mono to 8ch conversion.  S32_LE, Center.
TEST(FormatConverterOpsTest, MonoTo8chS32LECenter) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 8;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = 2;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_mono_to_71(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == center) {
        EXPECT_EQ(src[i], dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test mono to 8ch conversion.  S32_LE, LeftRight.
TEST(FormatConverterOpsTest, MonoTo8chS32LELeftRight) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 8;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = -1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_mono_to_71(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == left || k == right) {
        EXPECT_EQ(src[i] / 2, dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test mono to 8ch conversion.  S32_LE, Unknown.
TEST(FormatConverterOpsTest, MonoTo8chS32LEUnknown) {
  const size_t frames = 4096;
  const size_t in_ch = 1;
  const size_t out_ch = 8;
  const size_t left = -1;
  const size_t right = -1;
  const size_t center = -1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_mono_to_71(left, right, center, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == 0) {
        EXPECT_EQ(src[i], dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test stereo to 8ch conversion.  S32_LE, LeftRight.
TEST(FormatConverterOpsTest, StereoTo8chS32LELeftRight) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 8;
  const size_t left = 0;
  const size_t right = 1;
  const size_t center = -1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_stereo_to_71(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == left) {
        EXPECT_EQ(src[i * 2], dst[i * 8 + k]);
      } else if (k == right) {
        EXPECT_EQ(src[i * 2 + 1], dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test stereo to 8ch conversion.  S32_LE, Center.
TEST(FormatConverterOpsTest, StereoTo8chS32LECenter) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 8;
  const size_t left = -1;
  const size_t right = -1;
  const size_t center = 2;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_stereo_to_71(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == center) {
        EXPECT_EQ(S32AddAndClip(src[i * 2], src[i * 2 + 1]), dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test stereo to 8ch conversion.  S32_LE, Unknown.
TEST(FormatConverterOpsTest, StereoTo8chS32LEUnknown) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 8;
  const size_t left = -1;
  const size_t right = -1;
  const size_t center = -1;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_stereo_to_71(left, right, center, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < 8; ++k) {
      if (k == 0 || k == 1) {
        EXPECT_EQ(src[i * 2 + k], dst[i * 8 + k]);
      } else {
        EXPECT_EQ(0, dst[i * 8 + k]);
      }
    }
  }
}

// Test quad to 8ch conversion.  S32_LE, Specify.
TEST(FormatConverterOpsTest, QuadTo8chS32LESpecify) {
  const size_t frames = 4096;
  const size_t in_ch = 4;
  const size_t out_ch = 8;

  const size_t fl_quad = 0;
  const size_t fr_quad = 1;
  const size_t rl_quad = 2;
  const size_t rr_quad = 3;

  // Specify custom channel mapping
  const size_t fl_71 = 7;
  const size_t fr_71 = 6;
  const size_t center_71 = 5;
  const size_t lfe_71 = 4;
  const size_t rl_71 = 3;
  const size_t rr_71 = 2;
  const size_t sl_71 = 1;
  const size_t sr_71 = 0;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_quad_to_71(fl_71, fr_71, rl_71, rr_71, (uint8_t*)src.get(),
                              frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 4 + fl_quad], dst[i * 8 + fl_71]);
    EXPECT_EQ(src[i * 4 + fr_quad], dst[i * 8 + fr_71]);
    EXPECT_EQ(src[i * 4 + rl_quad], dst[i * 8 + rl_71]);
    EXPECT_EQ(src[i * 4 + rr_quad], dst[i * 8 + rr_71]);

    EXPECT_EQ(0, dst[i * 8 + center_71]);
    EXPECT_EQ(0, dst[i * 8 + lfe_71]);
    EXPECT_EQ(0, dst[i * 8 + sl_71]);
    EXPECT_EQ(0, dst[i * 8 + sr_71]);
  }
}

// Test quad to 8ch conversion.  S32_LE, Default.
TEST(FormatConverterOpsTest, QuadTo8chS32LEDefault) {
  const size_t frames = 4096;
  const size_t in_ch = 4;
  const size_t out_ch = 8;

  const size_t fl_quad = 0;
  const size_t fr_quad = 1;
  const size_t rl_quad = 2;
  const size_t rr_quad = 3;

  const size_t fl_71 = 0;
  const size_t fr_71 = 1;
  const size_t center_71 = 2;
  const size_t lfe_71 = 3;
  const size_t rl_71 = 4;
  const size_t rr_71 = 5;
  const size_t sl_71 = 6;
  const size_t sr_71 = 7;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_quad_to_71(-1, -1, -1, -1, (uint8_t*)src.get(), frames,
                              (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 4 + fl_quad], dst[i * 8 + fl_71]);
    EXPECT_EQ(src[i * 4 + fr_quad], dst[i * 8 + fr_71]);
    EXPECT_EQ(src[i * 4 + rl_quad], dst[i * 8 + rl_71]);
    EXPECT_EQ(src[i * 4 + rr_quad], dst[i * 8 + rr_71]);

    EXPECT_EQ(0, dst[i * 8 + center_71]);
    EXPECT_EQ(0, dst[i * 8 + lfe_71]);
    EXPECT_EQ(0, dst[i * 8 + sl_71]);
    EXPECT_EQ(0, dst[i * 8 + sr_71]);
  }
}

// Test 6ch to 8ch conversion.  S32_LE, Specify, Rear.
TEST(FormatConverterOpsTest, 6chTo8chS32LESpecifyRear) {
  const size_t frames = 4096;
  const size_t in_ch = 6;
  const size_t out_ch = 8;

  // FL FR FC LFE RL RR
  const struct cras_audio_format in_fmt = {
      .channel_layout = {0, 1, 4, 5, 2, 3, -1, -1, -1, -1, -1},
  };
  const struct cras_audio_format out_fmt = {
      .channel_layout = {0, 1, 2, 3, 4, 5, 6, 7, -1, -1, -1},
  };

  const size_t fl_51 = in_fmt.channel_layout[CRAS_CH_FL];
  const size_t fr_51 = in_fmt.channel_layout[CRAS_CH_FR];
  const size_t center_51 = in_fmt.channel_layout[CRAS_CH_FC];
  const size_t lfe_51 = in_fmt.channel_layout[CRAS_CH_LFE];
  const size_t rl_51 = in_fmt.channel_layout[CRAS_CH_RL];
  const size_t rr_51 = in_fmt.channel_layout[CRAS_CH_RR];

  // Specify custom channel mapping
  const size_t fl_71 = out_fmt.channel_layout[CRAS_CH_FL];
  const size_t fr_71 = out_fmt.channel_layout[CRAS_CH_FR];
  const size_t center_71 = out_fmt.channel_layout[CRAS_CH_FC];
  const size_t lfe_71 = out_fmt.channel_layout[CRAS_CH_LFE];
  const size_t rl_71 = out_fmt.channel_layout[CRAS_CH_RL];
  const size_t rr_71 = out_fmt.channel_layout[CRAS_CH_RR];
  const size_t sl_71 = out_fmt.channel_layout[CRAS_CH_SL];
  const size_t sr_71 = out_fmt.channel_layout[CRAS_CH_SR];

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_51_to_71(&in_fmt, &out_fmt, (uint8_t*)src.get(), frames,
                            (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 6 + fl_51], dst[i * 8 + fl_71]);
    EXPECT_EQ(src[i * 6 + fr_51], dst[i * 8 + fr_71]);
    EXPECT_EQ(src[i * 6 + center_51], dst[i * 8 + center_71]);
    EXPECT_EQ(src[i * 6 + lfe_51], dst[i * 8 + lfe_71]);
    EXPECT_EQ(src[i * 6 + rl_51], dst[i * 8 + rl_71]);
    EXPECT_EQ(src[i * 6 + rr_51], dst[i * 8 + rr_71]);

    EXPECT_EQ(0, dst[i * 8 + sl_71]);
    EXPECT_EQ(0, dst[i * 8 + sr_71]);
  }
}

// Test 6ch to 8ch conversion.  S32_LE, Specify, Side.
TEST(FormatConverterOpsTest, 6chTo8chS32LESpecifySide) {
  const size_t frames = 4096;
  const size_t in_ch = 6;
  const size_t out_ch = 8;

  // FL FR FC LFE SL SR
  const struct cras_audio_format in_fmt = {
      .channel_layout = {0, 1, -1, -1, 2, 3, 4, 5, -1, -1, -1},
  };
  const struct cras_audio_format out_fmt = {
      .channel_layout = {0, 1, 2, 3, 4, 5, 6, 7, -1, -1, -1},
  };

  const size_t fl_51 = in_fmt.channel_layout[CRAS_CH_FL];
  const size_t fr_51 = in_fmt.channel_layout[CRAS_CH_FR];
  const size_t center_51 = in_fmt.channel_layout[CRAS_CH_FC];
  const size_t lfe_51 = in_fmt.channel_layout[CRAS_CH_LFE];
  const size_t sl_51 = in_fmt.channel_layout[CRAS_CH_SL];
  const size_t sr_51 = in_fmt.channel_layout[CRAS_CH_SR];

  // Specify custom channel mapping
  const size_t fl_71 = out_fmt.channel_layout[CRAS_CH_FL];
  const size_t fr_71 = out_fmt.channel_layout[CRAS_CH_FR];
  const size_t center_71 = out_fmt.channel_layout[CRAS_CH_FC];
  const size_t lfe_71 = out_fmt.channel_layout[CRAS_CH_LFE];
  const size_t rl_71 = out_fmt.channel_layout[CRAS_CH_RL];
  const size_t rr_71 = out_fmt.channel_layout[CRAS_CH_RR];
  const size_t sl_71 = out_fmt.channel_layout[CRAS_CH_SL];
  const size_t sr_71 = out_fmt.channel_layout[CRAS_CH_SR];

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_51_to_71(&in_fmt, &out_fmt, (uint8_t*)src.get(), frames,
                            (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 6 + fl_51], dst[i * 8 + fl_71]);
    EXPECT_EQ(src[i * 6 + fr_51], dst[i * 8 + fr_71]);
    EXPECT_EQ(src[i * 6 + center_51], dst[i * 8 + center_71]);
    EXPECT_EQ(src[i * 6 + lfe_51], dst[i * 8 + lfe_71]);
    EXPECT_EQ(src[i * 6 + sl_51], dst[i * 8 + sl_71]);
    EXPECT_EQ(src[i * 6 + sr_51], dst[i * 8 + sr_71]);

    EXPECT_EQ(0, dst[i * 8 + rl_71]);
    EXPECT_EQ(0, dst[i * 8 + rr_71]);
  }
}

// Test 6ch to 8ch conversion.  S32_LE, Default.
TEST(FormatConverterOpsTest, 6chTo8chS32LEDefault) {
  const size_t frames = 4096;
  const size_t in_ch = 6;
  const size_t out_ch = 8;

  const struct cras_audio_format in_fmt = {
      .channel_layout = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
  };
  const struct cras_audio_format out_fmt = {
      .channel_layout = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
  };

  const size_t fl_51 = 0;
  const size_t fr_51 = 1;
  const size_t center_51 = 2;
  const size_t lfe_51 = 3;
  const size_t rl_51 = 4;
  const size_t rr_51 = 5;

  const size_t fl_71 = 0;
  const size_t fr_71 = 1;
  const size_t center_71 = 2;
  const size_t lfe_71 = 3;
  const size_t rl_71 = 4;
  const size_t rr_71 = 5;
  const size_t sl_71 = 6;
  const size_t sr_71 = 7;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_51_to_71(&in_fmt, &out_fmt, (uint8_t*)src.get(), frames,
                            (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    EXPECT_EQ(src[i * 6 + fl_51], dst[i * 8 + fl_71]);
    EXPECT_EQ(src[i * 6 + fr_51], dst[i * 8 + fr_71]);
    EXPECT_EQ(src[i * 6 + center_51], dst[i * 8 + center_71]);
    EXPECT_EQ(src[i * 6 + lfe_51], dst[i * 8 + lfe_71]);
    EXPECT_EQ(src[i * 6 + rl_51], dst[i * 8 + rl_71]);
    EXPECT_EQ(src[i * 6 + rr_51], dst[i * 8 + rr_71]);

    EXPECT_EQ(0, dst[i * 8 + sl_71]);
    EXPECT_EQ(0, dst[i * 8 + sr_71]);
  }
}

// Test Stereo to 3ch conversion.  S32_LE.
TEST(FormatConverterOpsTest, StereoTo3chS32LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 3;
  struct cras_audio_format fmt = {
      .format = SND_PCM_FORMAT_S32_LE,
      .frame_rate = 48000,
      .num_channels = 3,
  };

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_default_all_to_all(&fmt, in_ch, out_ch, (uint8_t*)src.get(),
                                      frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    int64_t sum = 0;
    for (size_t k = 0; k < in_ch; ++k) {
      sum += (int32_t)src[i * in_ch + k];
    }
    src[i * in_ch + 0] = (int32_t)(sum / (int64_t)in_ch);
  }
  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < out_ch; ++k) {
      EXPECT_EQ(src[i * in_ch + 0], dst[i * out_ch + k]);
    }
  }
}

// Test 6ch to 8ch conversion with all_to_all.  S32_LE.
TEST(FormatConverterOpsTest, 6chTo8chAllToAllS32LE) {
  const size_t frames = 65536;
  const size_t in_ch = 6;
  const size_t out_ch = 8;
  struct cras_audio_format fmt = {
      .format = SND_PCM_FORMAT_S32_LE,
      .frame_rate = 48000,
      .num_channels = 8,
  };

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);
  for (size_t i = 0; i < frames; ++i) {
    for (size_t k = 0; k < in_ch; k++) {
      src[i * in_ch + k] = (k == 0) ? (INT32_MIN + (int32_t)i) : 0;
    }
  }

  size_t ret = s32_default_all_to_all(&fmt, in_ch, out_ch, (uint8_t*)src.get(),
                                      frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    src[i * in_ch + 0] /= (int32_t)in_ch;
    for (size_t k = 0; k < out_ch; ++k) {
      EXPECT_EQ(src[i * in_ch + 0], dst[i * out_ch + k]);
    }
  }
}

// Test Multiply with Coef.  S32_LE.
TEST(FormatConverterOpsTest, MultiplyWithCoefS32LE) {
  const size_t buf_size = 4096;

  S32LEPtr buf = CreateS32LE(buf_size);
  FloatPtr coef = CreateFloat(buf_size);

  int32_t ret = s32_multiply_buf_with_coef(coef.get(), buf.get(), buf_size);

  int64_t exp = 0;
  for (size_t i = 0; i < buf_size; ++i) {
    exp += coef[i] * buf[i];
  }
  exp = MIN(MAX(exp, INT32_MIN), INT32_MAX);

  EXPECT_EQ((int32_t)exp, ret);
}

// Test Convert Channels.  S32_LE.
TEST(FormatConverterOpsTest, ConvertChannelsS32LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 3;

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);
  FloatPtr ch_conv_mtx = CreateFloat(out_ch * in_ch);
  std::unique_ptr<float*[]> mtx(new float*[out_ch]);
  for (size_t i = 0; i < out_ch; ++i) {
    mtx[i] = &ch_conv_mtx[i * in_ch];
  }

  size_t ret =
      s32_convert_channels(mtx.get(), in_ch, out_ch, (uint8_t*)src.get(),
                           frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t fr = 0; fr < frames; ++fr) {
    for (size_t i = 0; i < out_ch; ++i) {
      int64_t exp = 0;
      for (size_t k = 0; k < in_ch; ++k) {
        exp += mtx[i][k] * src[fr * in_ch + k];
      }
      exp = MIN(MAX(exp, INT32_MIN), INT32_MAX);
      EXPECT_EQ(exp, dst[fr * out_ch + i]);
    }
  }
}

// Test Stereo to 20ch conversion.  S32_LE.
TEST(FormatConverterOpsTest, TwoToTwentyS32LE) {
  const size_t frames = 4096;
  const size_t in_ch = 2;
  const size_t out_ch = 20;
  struct cras_audio_format fmt = {
      .format = SND_PCM_FORMAT_S32_LE,
      .frame_rate = 48000,
      .num_channels = 20,
  };

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_some_to_some(&fmt, in_ch, out_ch, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    size_t k;
    // Input channels should be directly copied over.
    for (k = 0; k < in_ch; ++k) {
      EXPECT_EQ(src[i * in_ch + k], dst[i * out_ch + k]);
    }
    // The rest should be zeroed.
    for (; k < out_ch; ++k) {
      EXPECT_EQ(0, dst[i * out_ch + k]);
    }
  }
}

// Test 20ch to Stereo.  S32_LE.
TEST(FormatConverterOpsTest, TwentyToTwoS32LE) {
  const size_t frames = 4096;
  const size_t in_ch = 20;
  const size_t out_ch = 2;
  struct cras_audio_format fmt = {
      .format = SND_PCM_FORMAT_S32_LE,
      .frame_rate = 48000,
      .num_channels = 2,
  };

  S32LEPtr src = CreateS32LE(frames * in_ch);
  S32LEPtr dst = CreateS32LE(frames * out_ch);

  size_t ret = s32_some_to_some(&fmt, in_ch, out_ch, (uint8_t*)src.get(),
                                frames, (uint8_t*)dst.get());
  EXPECT_EQ(ret, frames);

  for (size_t i = 0; i < frames; ++i) {
    size_t k;
    // Input channels should be directly copied over.
    for (k = 0; k < out_ch; ++k) {
      EXPECT_EQ(src[i * in_ch + k], dst[i * out_ch + k]);
    }
  }
}

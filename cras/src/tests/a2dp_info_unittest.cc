// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
  #include <sbc/sbc.h>

  #include "cras_sbc_codec.h"
  #include "cras_a2dp_info.h"
}

static size_t cras_sbc_codec_create_called;
static size_t cras_sbc_codec_destroy_called;
static uint8_t codec_create_freq_val;
static uint8_t codec_create_mode_val;
static uint8_t codec_create_subbands_val;
static uint8_t codec_create_alloc_val;
static uint8_t codec_create_blocks_val;
static uint8_t codec_create_bitpool_val;
static a2dp_sbc_t sbc;

void ResetStubData() {
  cras_sbc_codec_create_called = 0;

  codec_create_freq_val = 0;
  codec_create_mode_val = 0;
  codec_create_subbands_val = 0;
  codec_create_alloc_val = 0;
  codec_create_blocks_val = 0;
  codec_create_bitpool_val = 0;

  sbc.frequency = SBC_SAMPLING_FREQ_48000;
  sbc.channel_mode = SBC_CHANNEL_MODE_JOINT_STEREO;
  sbc.allocation_method = SBC_ALLOCATION_LOUDNESS;
  sbc.subbands = SBC_SUBBANDS_8;
  sbc.block_length = SBC_BLOCK_LENGTH_16;
  sbc.max_bitpool = 50;
}

namespace {

TEST(A2dpInfoInit, InitA2dp) {
  ResetStubData();
  init_a2dp(NULL, &sbc);

  ASSERT_EQ(1, cras_sbc_codec_create_called);
  ASSERT_EQ(SBC_FREQ_48000, codec_create_freq_val);
  ASSERT_EQ(SBC_MODE_JOINT_STEREO, codec_create_mode_val);
  ASSERT_EQ(SBC_AM_LOUDNESS, codec_create_alloc_val);
  ASSERT_EQ(SBC_SB_8, codec_create_subbands_val);
  ASSERT_EQ(SBC_BLK_16, codec_create_blocks_val);
  ASSERT_EQ(50, codec_create_bitpool_val);
}

TEST(A2dpInfoInit, DestroyA2dp) {
  ResetStubData();
  destroy_a2dp(NULL);

  ASSERT_EQ(1, cras_sbc_codec_destroy_called);
}

} // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

struct cras_audio_codec *cras_sbc_codec_create(uint8_t freq,
		uint8_t mode, uint8_t subbands, uint8_t alloc,
		uint8_t blocks, uint8_t bitpool)
{
  cras_sbc_codec_create_called++;
  codec_create_freq_val = freq;
  codec_create_mode_val = mode;
  codec_create_subbands_val = subbands;
  codec_create_alloc_val = alloc;
  codec_create_blocks_val = blocks;
  codec_create_bitpool_val = bitpool;
  return NULL;
}

void cras_sbc_codec_destroy(struct cras_audio_codec *codec)
{
  cras_sbc_codec_destroy_called++;
}

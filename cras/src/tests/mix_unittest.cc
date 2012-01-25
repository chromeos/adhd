// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_shm.h"
#include "cras_mix.h"
#include "cras_types.h"

}

namespace {

static const size_t kBufferFrames = 8192;

class MixTestSuite : public testing::Test{
  protected:
    virtual void SetUp() {
      mix_buffer_ = (int16_t *)malloc(kBufferFrames * 4);
      test_buffer_ = (int16_t *)malloc(kBufferFrames * 4);
      mix_index_ = 0;
    }

    virtual void TearDown() {
      free(mix_buffer_);
      free(test_buffer_);
    }

  int16_t *mix_buffer_;
  int16_t *test_buffer_;
  size_t mix_index_;
};

TEST_F(MixTestSuite, MixBuffers) {
  int16_t *compare_buffer;

  for (size_t i = 0; i < kBufferFrames; i++)
    test_buffer_[i] = i;

  cras_mix_add_buffer(mix_buffer_, test_buffer_, kBufferFrames*2, &mix_index_);
  EXPECT_EQ(0, mix_index_);
  EXPECT_EQ(mix_buffer_[0], test_buffer_[0]);
  EXPECT_EQ(0, memcmp(mix_buffer_, test_buffer_, kBufferFrames*4));

  mix_index_ = 1;
  cras_mix_add_buffer(mix_buffer_, test_buffer_, kBufferFrames*2, &mix_index_);
  EXPECT_EQ(1, mix_index_);
  EXPECT_EQ(mix_buffer_[0], test_buffer_[0] * 2);
  compare_buffer = (int16_t *)malloc(kBufferFrames * 4);
  for (size_t i = 0; i < kBufferFrames; i++)
    compare_buffer[i] = test_buffer_[i] * 2;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer, kBufferFrames*4));
  free(compare_buffer);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

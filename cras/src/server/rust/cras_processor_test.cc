// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "cras/src/audio_processor/c/plugin_processor.h"
#include "cras/src/server/rust/include/cras_processor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

TEST(CrasProcessor, Negate) {
  CrasProcessorConfig cfg = {
      .channels = 1,
      .block_size = 480,
      .frame_rate = 48000,
      .effect = CrasProcessorEffect::Negate,
  };

  plugin_processor* processor = cras_processor_create(&cfg);
  ASSERT_THAT(processor, testing::NotNull());

  // Process audio a few times to make catch obvious memory problems.
  for (int i = 0; i < 3; i++) {
    std::vector<float> input_buffer(480);
    std::vector<float> expected_output(480);
    for (int i = 0; i < 480; i++) {
      input_buffer[i] = i * 0.001;

      // Running a negate processor. Output should be -input.
      expected_output[i] = -i * 0.001;
    }

    multi_slice input = {
        .channels = 1,
        .num_frames = 480,
        .data = {input_buffer.data()},
    };
    multi_slice output = {};
    ASSERT_EQ(processor->ops->run(processor, &input, &output), StatusOk);

    ASSERT_EQ(output.channels, 1);
    ASSERT_EQ(output.num_frames, 480);
    ASSERT_THAT(output.data[0], testing::NotNull());

    // TODO: Use span when we have absl or C++20.
    ASSERT_EQ(std::vector<float>(output.data[0], output.data[0] + 480),
              expected_output);
  }

  processor->ops->destroy(processor);
}

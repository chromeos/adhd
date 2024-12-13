// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "audio_processor/c/plugin_processor.h"
#include "cras/server/processor/processor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

static enum status noop_processor_run(struct plugin_processor* p,
                                      const struct multi_slice* input,
                                      struct multi_slice* output) {
  *output = *input;
  return StatusOk;
}

static enum status noop_processor_destroy(struct plugin_processor* p) {
  return StatusOk;
}

static enum status noop_processor_get_output_frame_rate(
    struct plugin_processor* p,
    size_t* output_frame_rate) {
  *output_frame_rate = 0;
  return StatusOk;
}

static const struct plugin_processor_ops noop_processor_ops = {
    .run = noop_processor_run,
    .destroy = noop_processor_destroy,
    .get_output_frame_rate = noop_processor_get_output_frame_rate,
};

static struct plugin_processor noop_processor = {.ops = &noop_processor_ops};

struct CrasProcessorParam {
  std::string name;
  CrasProcessorEffect effect;
  struct plugin_processor* apm;
  double expected_output_mult;
};

class CrasProcessor : public testing::TestWithParam<CrasProcessorParam> {};

INSTANTIATE_TEST_SUITE_P(
    ,
    CrasProcessor,
    testing::Values((CrasProcessorParam{
                        .name = "negate_noop",
                        .effect = Negate,
                        .apm = &noop_processor,
                        .expected_output_mult = -1,
                    }),
                    (CrasProcessorParam{
                        .name = "negate_nullptr",
                        .effect = Negate,
                        .apm = nullptr,
                        .expected_output_mult = -1,
                    }),
                    (CrasProcessorParam{
                        .name = "noeffects_noop",
                        .effect = NoEffects,
                        .apm = &noop_processor,
                        .expected_output_mult = 1,
                    }),
                    (CrasProcessorParam{
                        .name = "noeffects_nullptr",
                        .effect = NoEffects,
                        .apm = nullptr,
                        .expected_output_mult = 1,
                    })),
    [](const testing::TestParamInfo<CrasProcessor::ParamType>& info) {
      return info.param.name;
    });

TEST_P(CrasProcessor, Simple) {
  CrasProcessorConfig cfg = {
      .channels = 1,
      .block_size = 480,
      .frame_rate = 48000,
      .effect = GetParam().effect,
  };

  auto r = cras_processor_create(&cfg, GetParam().apm);
  plugin_processor* processor = r.plugin_processor;
  ASSERT_EQ(r.effect, cfg.effect);
  ASSERT_THAT(processor, testing::NotNull());

  // Process audio a few times to make catch obvious memory problems.
  for (int i = 0; i < 3; i++) {
    std::vector<float> input_buffer(480);
    std::vector<float> expected_output(480);
    for (int i = 0; i < 480; i++) {
      input_buffer[i] = i * 0.001;

      expected_output[i] = GetParam().expected_output_mult * i * 0.001;
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

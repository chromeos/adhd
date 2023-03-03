// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <random>
#include <vector>

#include "benchmark/benchmark.h"
#include "cras/src/benchmark/benchmark_util.h"

namespace {
extern "C" {
#include "cras/src/dsp/drc.h"
#include "cras/src/dsp/eq2.h"
}

constexpr int NUM_CHANNELS = 2;

class BM_Dsp : public benchmark::Fixture {
 public:
  void SetUp(const ::benchmark::State& state) {
    std::random_device rnd_device;
    std::mt19937 engine{rnd_device()};
    frames = state.range(0);
    samples = gen_float_samples(frames * NUM_CHANNELS, engine);
  }

  void TearDown(const ::benchmark::State& state) {}

  // Number of |frames|
  size_t frames;
  // |frames| * |NUM_CHANNELS| of samples.
  std::vector<float> samples;
};

BENCHMARK_DEFINE_F(BM_Dsp, Eq2)(benchmark::State& state) {
  const double NQ = 44100 / 2;  // nyquist frequency
  // eq chain
  struct eq2* eq2 = eq2_new();
  eq2_append_biquad(eq2, 0, BQ_PEAKING, 380 / NQ, 3, -10);
  eq2_append_biquad(eq2, 0, BQ_PEAKING, 720 / NQ, 3, -12);
  eq2_append_biquad(eq2, 0, BQ_PEAKING, 1705 / NQ, 3, -8);
  eq2_append_biquad(eq2, 0, BQ_HIGHPASS, 218 / NQ, 0.7, -10.2);
  eq2_append_biquad(eq2, 0, BQ_PEAKING, 580 / NQ, 6, -8);
  eq2_append_biquad(eq2, 0, BQ_HIGHSHELF, 8000 / NQ, 3, 2);
  eq2_append_biquad(eq2, 1, BQ_PEAKING, 450 / NQ, 3, -12);
  eq2_append_biquad(eq2, 1, BQ_PEAKING, 721 / NQ, 3, -12);
  eq2_append_biquad(eq2, 1, BQ_PEAKING, 1800 / NQ, 8, -10.2);
  eq2_append_biquad(eq2, 1, BQ_PEAKING, 580 / NQ, 6, -8);
  eq2_append_biquad(eq2, 1, BQ_HIGHPASS, 250 / NQ, 0.6578, 0);
  eq2_append_biquad(eq2, 1, BQ_HIGHSHELF, 8000 / NQ, 0, 2);
  for (auto _ : state) {
    eq2_process(eq2, samples.data(), samples.data() + frames, frames);
  }
  eq2_free(eq2);
  state.counters["frames_per_second"] = benchmark::Counter(
      int64_t(state.iterations()) * frames, benchmark::Counter::kIsRate);
  state.counters["time_per_48k_frames"] = benchmark::Counter(
      int64_t(state.iterations()) * frames / 48000,
      benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

BENCHMARK_REGISTER_F(BM_Dsp, Eq2)->RangeMultiplier(2)->Range(256, 8 << 10);

BENCHMARK_DEFINE_F(BM_Dsp, Drc)(benchmark::State& state) {
  const double NQ = 44100 / 2;  // nyquist frequency

  struct drc* drc = drc_new(44100);
  drc->emphasis_disabled = 0;
  drc_set_param(drc, 0, PARAM_CROSSOVER_LOWER_FREQ, 0);
  drc_set_param(drc, 0, PARAM_ENABLED, 1);
  drc_set_param(drc, 0, PARAM_THRESHOLD, -29);
  drc_set_param(drc, 0, PARAM_KNEE, 3);
  drc_set_param(drc, 0, PARAM_RATIO, 6.677);
  drc_set_param(drc, 0, PARAM_ATTACK, 0.02);
  drc_set_param(drc, 0, PARAM_RELEASE, 0.2);
  drc_set_param(drc, 0, PARAM_POST_GAIN, -7);

  drc_set_param(drc, 1, PARAM_CROSSOVER_LOWER_FREQ, 200 / NQ);
  drc_set_param(drc, 1, PARAM_ENABLED, 1);
  drc_set_param(drc, 1, PARAM_THRESHOLD, -32);
  drc_set_param(drc, 1, PARAM_KNEE, 23);
  drc_set_param(drc, 1, PARAM_RATIO, 12);
  drc_set_param(drc, 1, PARAM_ATTACK, 0.02);
  drc_set_param(drc, 1, PARAM_RELEASE, 0.2);
  drc_set_param(drc, 1, PARAM_POST_GAIN, 0.7);

  drc_set_param(drc, 2, PARAM_CROSSOVER_LOWER_FREQ, 1200 / NQ);
  drc_set_param(drc, 2, PARAM_ENABLED, 1);
  drc_set_param(drc, 2, PARAM_THRESHOLD, -24);
  drc_set_param(drc, 2, PARAM_KNEE, 30);
  drc_set_param(drc, 2, PARAM_RATIO, 1);
  drc_set_param(drc, 2, PARAM_ATTACK, 0.001);
  drc_set_param(drc, 2, PARAM_RELEASE, 1);
  drc_set_param(drc, 2, PARAM_POST_GAIN, 0);

  drc_init(drc);
  for (auto _ : state) {
    for (size_t start = 0; start < frames;) {
      float* data[2] = {samples.data() + start,
                        samples.data() + frames + start};
      const int chunk = std::min(DRC_PROCESS_MAX_FRAMES, (int)(frames - start));
      drc_process(drc, data, chunk);
      start += chunk;
    }
  }
  drc_free(drc);

  state.counters["frames_per_second"] = benchmark::Counter(
      int64_t(state.iterations()) * frames, benchmark::Counter::kIsRate);
  state.counters["time_per_48k_frames"] = benchmark::Counter(
      int64_t(state.iterations()) * frames / 48000,
      benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

BENCHMARK_REGISTER_F(BM_Dsp, Drc)->RangeMultiplier(2)->Range(256, 8 << 10);

}  // namespace

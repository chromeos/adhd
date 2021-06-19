// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <random>
#include <vector>

#include <benchmark/benchmark.h>

namespace {
extern "C" {
#include "src/dsp/eq2.h"
}

// Generates a vector of |float| samples with given |size| and random
// |engine|.
std::vector<float> gen_float_samples(size_t size, std::mt19937& engine) {
  std::uniform_real_distribution<float> dist(-1.0, 1.0);
  std::vector<float> samples(size);
  auto gen = [&dist, &engine]() { return dist(engine); };
  std::generate(samples.begin(), samples.end(), gen);
  return samples;
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
}  // namespace

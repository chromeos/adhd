// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include "benchmark/benchmark.h"
#include "cras/src/benchmark/benchmark_util.h"

namespace {
extern "C" {
#include "cras/src/server/cras_mix.h"
}

static void BM_CrasMixerOpsScaleBuffer(benchmark::State& state) {
  cras_mix_init();

  std::random_device rnd_device;
  std::mt19937 engine{rnd_device()};
  std::vector<int16_t> samples = gen_s16_le_samples(state.range(0), engine);
  std::uniform_real_distribution<double> distribution(0.0000001, 0.9999999);
  for (auto _ : state) {
    double scale = distribution(engine);
    cras_scale_buffer(SND_PCM_FORMAT_S16_LE, (uint8_t*)(samples.data()),
                      state.range(0), scale);
  }
  state.SetBytesProcessed(int64_t(state.iterations()) *
                          int64_t(state.range(0)) * /*bytes per sample=*/2);
}

BENCHMARK(BM_CrasMixerOpsScaleBuffer)->RangeMultiplier(2)->Range(256, 8 << 10);

static void BM_CrasMixerOpsMixAdd(benchmark::State& state) {
  cras_mix_init();

  std::random_device rnd_device;
  std::mt19937 engine{rnd_device()};
  std::vector<int16_t> src = gen_s16_le_samples(state.range(0), engine);
  std::vector<int16_t> dst = gen_s16_le_samples(state.range(0), engine);
  std::uniform_real_distribution<double> distribution(0.5, 2);
  for (auto _ : state) {
    double scale = distribution(engine);
    cras_mix_add(SND_PCM_FORMAT_S16_LE, (uint8_t*)dst.data(),
                 (uint8_t*)src.data(), state.range(0), 0, 0, scale);
  }
  state.SetBytesProcessed(int64_t(state.iterations()) *
                          int64_t(state.range(0) * /*bytes per sample=*/2));
}

BENCHMARK(BM_CrasMixerOpsMixAdd)->RangeMultiplier(2)->Range(256, 8 << 10);
}  // namespace

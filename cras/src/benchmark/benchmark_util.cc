// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/benchmark/benchmark_util.h"

#include <algorithm>
#include <cstdint>
#include <random>

std::vector<float> gen_float_samples(size_t size, std::mt19937& engine) {
  std::uniform_real_distribution<float> dist(-1.0, 1.0);
  std::vector<float> samples(size);
  auto gen = [&dist, &engine]() { return dist(engine); };
  std::generate(samples.begin(), samples.end(), gen);
  return samples;
}

std::vector<int16_t> gen_s16_le_samples(size_t size, std::mt19937& engine) {
  std::uniform_int_distribution<int16_t> dist(-32768, 32767);
  std::vector<int16_t> samples(size);
  auto gen = [&dist, &engine]() { return dist(engine); };
  std::generate(samples.begin(), samples.end(), gen);
  return samples;
}

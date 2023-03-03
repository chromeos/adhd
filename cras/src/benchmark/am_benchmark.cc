// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "benchmark/benchmark.h"
#include "cras/src/benchmark/benchmark_util.h"
#include "cras/src/dsp/am.h"

namespace {

class BM_Am : public benchmark::Fixture {
 public:
  void SetUp(const ::benchmark::State& state) {
    frames = 480;
    std::random_device rnd_device;
    std::mt19937 engine{rnd_device()};
    samples = gen_float_samples(frames, engine);
    output_buf = std::vector<float>(frames, 0);

    int microseconds = state.range(0);
    sleep_duration = std::chrono::duration<double, std::micro>{
        static_cast<double>(microseconds)};
  }

  void TearDown(const ::benchmark::State& state) {}

  // Number of |frames|.
  size_t frames;
  // Sleep time in microseconds.
  std::chrono::duration<double, std::micro> sleep_duration;
  // |frames| of samples.
  std::vector<float> samples;
  // |frames| of samples.
  std::vector<float> output_buf;
};

BENCHMARK_DEFINE_F(BM_Am, SR)(benchmark::State& state) {
  const char model_path[] =
      "/run/imageloader/sr-bt-dlc/package/root/btnb.tflite";
  struct am_context* ctx = am_new(model_path);
  if (ctx == nullptr) {
    state.SkipWithError("Model does not exist!");
    return;
  }

  for (auto _ : state) {
    // Use manual timer to skip sleep period
    auto start = std::chrono::high_resolution_clock::now();
    int rc = 0;
    benchmark::DoNotOptimize(rc = am_process(ctx, samples.data(), frames,
                                             output_buf.data(), frames));
    if (rc != 0) {
      state.SkipWithError("am_process error.");
      break;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_seconds =
        std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
    state.SetIterationTime(elapsed_seconds.count());
    // Sleep here to simulate audio thread behavior
    std::this_thread::sleep_for(sleep_duration);
  }
  state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(frames) *
                          /*bytes per sample=*/2);
  am_free(ctx);
}

BENCHMARK_REGISTER_F(BM_Am, SR)
    ->Arg(5000)
    ->Arg(10000)
    ->Arg(20000)
    ->Arg(40000)
    ->UseManualTime();

}  // namespace

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
#include "cras/src/dsp/dsp_util.h"
#include "cras_audio_format.h"
#include "webrtc-apm/webrtc_apm.h"
}

class BM_Apm : public benchmark::Fixture {
 public:
  void SetUp(const ::benchmark::State& state) {
    std::random_device rnd_device;
    std::mt19937 engine{rnd_device()};
    rate = state.range(0);
    WebRtcApmFeatures features;
    features.agc2_enabled = state.range(1) == 1;
    /* APM processes data in 10ms block. Sample rate 48000 means
     * 480 frames of block size.
     */
    block_sz = rate / 100;
    apm = webrtc_apm_create_for_testing(
        /*num_channels=*/2, rate, /*aec_ini=*/NULL, /*apm_ini=*/NULL,
        /*enforce_aec_on=*/true, /*enforce_ns_on=*/false,
        /*enforce_agc_on=*/true, features);
    int_samples = gen_s16_le_samples(block_sz * 2 * 2, engine);
    float_samples = gen_float_samples(block_sz * 2 * 2, engine);
  }

  void TearDown(benchmark::State& state) {
    webrtc_apm_destroy(apm);
    state.counters["frames_per_second"] = benchmark::Counter(
        int64_t(state.iterations()) * block_sz, benchmark::Counter::kIsRate);
    state.counters["time_per_one_second_data"] = benchmark::Counter(
        int64_t(state.iterations()) * block_sz / rate,
        benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
  }

  size_t block_sz;
  size_t rate;
  webrtc_apm apm;
  std::vector<int16_t> int_samples;
  std::vector<float> float_samples;
};

/*
 * APM(Audio processing module) processes input and output data
 * in order to find the audio content just played and got recorded
 * into the input and cancel it.
 * This benchmark covers the stadard APM processing on interleaved
 * float samples of different length controlled by |state|
 */
BENCHMARK_DEFINE_F(BM_Apm, ProcessBuffer)(benchmark::State& state) {
  float* fp[2];

  for (auto _ : state) {
    // Configure the interleaved float array to start of |samples|
    fp[0] = float_samples.data();
    fp[1] = fp[0] + block_sz;
    webrtc_apm_process_stream_f(apm, 2, rate, fp);

    fp[0] += /* stereo */ 2 * block_sz;
    fp[1] += 2 * block_sz;
    webrtc_apm_process_reverse_stream_f(apm, 2, rate, fp);
  }
}

BENCHMARK_REGISTER_F(BM_Apm, ProcessBuffer)
    ->ArgsProduct({{16000, 32000, 44100, 48000}, {0, 1}});

/* This benchmark evaluates the APM process plus the interleave
 * and deinterleave calculation from/to int16_t samples.
 */
BENCHMARK_DEFINE_F(BM_Apm, InterleaveAndProcess)(benchmark::State& state) {
  uint8_t* buf;
  float* fp[2];
  fp[0] = float_samples.data();
  fp[1] = fp[0] + block_sz;

  for (auto _ : state) {
    buf = (uint8_t*)int_samples.data();
    dsp_util_deinterleave(buf, fp, 2, SND_PCM_FORMAT_S16_LE, block_sz);
    webrtc_apm_process_stream_f(apm, 2, rate, fp);
    dsp_util_interleave(fp, buf, 2, SND_PCM_FORMAT_S16_LE, block_sz);

    buf += /* Two bytes per sample */ 2 * /* stereo */ 2 * block_sz;
    dsp_util_deinterleave(buf, fp, 2, SND_PCM_FORMAT_S16_LE, block_sz);
    webrtc_apm_process_reverse_stream_f(apm, 2, rate, fp);
    dsp_util_interleave(fp, buf, 2, SND_PCM_FORMAT_S16_LE, block_sz);
  }
}

BENCHMARK_REGISTER_F(BM_Apm, InterleaveAndProcess)
    ->ArgsProduct({{16000, 32000, 44100, 48000}, {0, 1}});
}  // namespace

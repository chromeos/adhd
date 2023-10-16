// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark/benchmark.h"
#include "cras/src/benchmark/benchmark_util.hh"
#include "cras/src/server/cras_alsa_helpers.h"
#include "cras/src/server/cras_alsa_ucm.h"
#include "cras/src/server/cras_mix_ops.h"
#include "cras_client.h"
#include "cras_iodev_info.h"
#include "cras_types.h"

namespace {

std::string read_file_to_string(const std::string& path) {
  std::string str;
  std::ifstream f(path);
  while (f.good()) {
    char buf[4096];
    f.read(buf, sizeof(buf));
    if (f.gcount() > 0) {
      str += std::string_view(buf, f.gcount());
    }
  }
  return str;
}

class BM_Alsa : public benchmark::Fixture {
 public:
  enum PCM_DEVICE {
    SPEAKER,
    HEADPHONE,
  };

  inline const char* ToString(PCM_DEVICE value) {
    switch (value) {
      case SPEAKER:
        return "Speaker";
      case HEADPHONE:
        return "Headphone";
    }

    fprintf(stderr, "invalid PCM_DEVICE: %d.\n", value);
    return NULL;
  }

  /* Returns the card name and card index in tuple, ex:
   * ("sof-cml_max98390_da7219" , "0") */
  std::tuple<std::string, std::string> get_card_info(
      BM_Alsa::PCM_DEVICE device) {
    struct cras_client* client;
    struct cras_iodev_info* target_dev = NULL;
    struct cras_ionode_info* target_node = NULL;
    struct cras_iodev_info devs[CRAS_MAX_IODEVS];
    struct cras_ionode_info nodes[CRAS_MAX_IONODES];
    size_t num_devs = CRAS_MAX_IODEVS;
    size_t num_nodes = CRAS_MAX_IONODES;
    std::string card_name = "";
    std::string card_idx = "";
    std::string target_dev_name = "";
    int i = 0;

    int rc;

    rc = cras_client_create_with_type(&client, CRAS_CONTROL);
    if (rc) {
      fprintf(stderr, "Couldn't create to cras_client, rc = %d.\n", rc);
      return std::make_tuple("", "");
    }
    rc = cras_client_run_thread(client);
    if (rc) {
      fprintf(stderr, "cras_client_run_thread failed, rc = %d.\n", rc);
      goto end;
    }
    rc = cras_client_connected_wait(client);
    if (rc) {
      fprintf(stderr, "Couldn't connect to server, rc = %d.\n", rc);
      goto end;
    }
    rc = cras_client_get_output_devices(client, devs, nodes, &num_devs,
                                        &num_nodes);
    for (i = 0; i < num_nodes; i++) {
      if (strcmp(nodes[i].name, ToString(device)) == 0) {
        target_node = &nodes[i];
        break;
      }
    }

    if (target_node == NULL) {
      fprintf(stderr, "Couldn't find target node.\n");
      goto end;
    }

    for (i = 0; i < num_devs; i++) {
      if (devs[i].idx == target_node->iodev_idx) {
        target_dev = &devs[i];
      }
    }

    if (target_dev == NULL) {
      fprintf(stderr, "Couldn't find target device.\n");
      goto end;
    }

    // target_dev->name example formt: sc7180-rt5682-max98357a-1mic: :0,1.
    target_dev_name = std::string(target_dev->name);
    card_name = target_dev_name.substr(0, target_dev_name.find(":"));
    card_idx = target_dev_name.substr(target_dev_name.find(":") + 3);
    card_idx.erase(card_idx.find(","));

  end:
    cras_client_destroy(client);
    return std::make_tuple(card_name, card_idx);
  }

  // Returns the pcm device name, ex: hw:0,0
  std::string get_pcm_name(BM_Alsa::PCM_DEVICE device) {
    struct cras_use_case_mgr* ucm_mgr;
    std::string ucm_suffix;
    ucm_suffix =
        read_file_to_string("/run/chromeos-config/v1/audio/main/ucm-suffix");

    auto card_info = get_card_info(device);
    std::string card_name = std::get<0>(card_info);
    std::string card_idx = std::get<1>(card_info);
    if (card_name == "" || card_idx == "") {
      fprintf(stderr, "Couldn't get card info.\n");
      return "";
    }
    std::string ucm_config = card_name;
    if (ucm_suffix != "") {
      ucm_config = ucm_config + "." + ucm_suffix;
    }
    ucm_mgr = ucm_create(ucm_config.c_str());
    if (ucm_mgr == nullptr) {
      fprintf(stderr, "Cannot ucm_create(\"%s\")\n", ucm_config.c_str());
      return "";
    }
    int dev_idx = ucm_get_alsa_dev_idx_for_dev(ucm_mgr, ToString(device),
                                               CRAS_STREAM_OUTPUT);

    std::string pcm_name = "hw:" + card_idx + "," + std::to_string(dev_idx);

    return pcm_name;
  }

  void SetUp(benchmark::State& state) {
    BM_Alsa::PCM_DEVICE device =
        static_cast<BM_Alsa::PCM_DEVICE>(state.range(0));
    pcm_name = get_pcm_name(device);
    if (pcm_name == "") {
      state.SkipWithError("Couldn't get pcm_name.");
    }

    int rc =
        cras_alsa_pcm_open(&handle, pcm_name.c_str(), SND_PCM_STREAM_PLAYBACK);
    if (rc < 0) {
      auto msg = "cras_alsa_pcm_open: " + pcm_name +
                 " failed. rc = " + std::to_string(rc);
      state.SkipWithError(msg.c_str());
      return;
    }
    rc = cras_alsa_set_hwparams(handle, &format, &buffer_frames, 0, 0);
    if (rc < 0) {
      auto msg = "cras_alsa_set_hwparams failed. rc = " + std::to_string(rc);
      state.SkipWithError(msg.c_str());
      cras_alsa_pcm_close(handle);
      return;
    }
    rc = cras_alsa_mmap_begin(handle, format_bytes, &buffer, &offset, &frames);
    if (rc < 0) {
      auto msg = "cras_alsa_mmap_begin failed. rc = " + std::to_string(rc);
      state.SkipWithError(msg.c_str());
      cras_alsa_pcm_close(handle);
      return;
    }

    int_samples = gen_s16_le_samples(frames * channels, engine);
    n_bytes = frames * channels * format_bytes;
    std::uniform_real_distribution<double> distribution(0.0000001, 0.9999999);
    scale = distribution(engine);

    return;
  }

  void TearDown(benchmark::State& state) {
    if (handle) {
      memset(buffer, 0, n_bytes);
      cras_alsa_mmap_commit(handle, offset, frames);
      cras_alsa_pcm_close(handle);
    }
  }

  std::string pcm_name;
  struct cras_audio_format format = {
      SND_PCM_FORMAT_S16_LE,
      48000,
      2,
  };
  snd_pcm_t* handle = NULL;
  snd_pcm_uframes_t buffer_frames = 0;
  std::vector<int16_t> int_samples;
  std::random_device rnd_device;
  std::mt19937 engine{rnd_device()};
  uint8_t* buffer;
  snd_pcm_uframes_t offset, frames = 4096;
  const unsigned channels = 2;
  const unsigned int format_bytes = 2;
  double scale;
  int n_bytes;
};

/* This benchmark evaluates the performace of accessing the buffer created by
 * snd_pcm_mmap_* API.
 */
BENCHMARK_DEFINE_F(BM_Alsa, MmapBufferAccess)(benchmark::State& state) {
  memcpy(buffer, int_samples.data(), n_bytes);
  double max_elapsed_seconds = 0.0;
  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    mixer_ops.scale_buffer(SND_PCM_FORMAT_S16_LE, buffer, frames * channels,
                           scale);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed_seconds =
        std::chrono::duration_cast<std::chrono::duration<double>>(end - start);

    state.SetIterationTime(elapsed_seconds.count());
    max_elapsed_seconds = fmax(max_elapsed_seconds, elapsed_seconds.count());
  }

  state.counters["frames_per_second"] = benchmark::Counter(
      int64_t(state.iterations()) * frames, benchmark::Counter::kIsRate);

  state.counters["time_per_4096_frames"] = benchmark::Counter(
      int64_t(state.iterations()),
      benchmark::Counter::kIsRate | benchmark::Counter::kInvert);

  state.counters["max_time_per_4096_frames"] = max_elapsed_seconds;
}

BENCHMARK_DEFINE_F(BM_Alsa, MmapBufferCopy)(benchmark::State& state) {
  double max_elapsed_seconds = 0.0;
  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    mixer_ops.scale_buffer(SND_PCM_FORMAT_S16_LE, (uint8_t*)int_samples.data(),
                           frames * channels, scale);
    memcpy(buffer, int_samples.data(), n_bytes);

    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed_seconds =
        std::chrono::duration_cast<std::chrono::duration<double>>(end - start);

    state.SetIterationTime(elapsed_seconds.count());
    max_elapsed_seconds = fmax(max_elapsed_seconds, elapsed_seconds.count());
  }
  state.counters["frames_per_second"] = benchmark::Counter(
      int64_t(state.iterations()) * frames, benchmark::Counter::kIsRate);

  state.counters["time_per_4096_frames"] = benchmark::Counter(
      int64_t(state.iterations()),
      benchmark::Counter::kIsRate | benchmark::Counter::kInvert);

  state.counters["max_time_per_4096_frames"] = max_elapsed_seconds;
}

BENCHMARK_REGISTER_F(BM_Alsa, MmapBufferAccess)
    ->Name("BM_Alsa/MmapBufferAccess/Speaker")
    ->UseManualTime()
    ->Arg(0);
BENCHMARK_REGISTER_F(BM_Alsa, MmapBufferAccess)
    ->Name("BM_Alsa/MmapBufferAccess/Headphone")
    ->UseManualTime()
    ->Arg(1);
BENCHMARK_REGISTER_F(BM_Alsa, MmapBufferCopy)
    ->Name("BM_Alsa/MmapBufferCopy/Speaker")
    ->UseManualTime()
    ->Arg(0);
BENCHMARK_REGISTER_F(BM_Alsa, MmapBufferCopy)
    ->Name("BM_Alsa/MmapBufferCopy/Headphone")
    ->UseManualTime()
    ->Arg(1);

}  // namespace

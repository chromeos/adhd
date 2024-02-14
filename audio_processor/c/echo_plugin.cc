// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_plugin.h"

#include <algorithm>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

#include "plugin_processor.h"

constexpr float kEchoDelaySec = 0.5f;
constexpr float kEchoDecayMultlipier = 0.5f;

// EchoProcessor implements an echo effect.
// Formula: y[n] = x[n] + y[n - delayFrames] * decay
class EchoProcessor {
 public:
  explicit EchoProcessor(const struct plugin_processor_config& config)
      : config_(config), frames_(config.frame_rate * kEchoDelaySec) {
    buffer_.resize(config_.channels);
    for (size_t ch = 0; ch < config_.channels; ch++) {
      buffer_[ch].resize(frames_);
    }
  }

  enum status Run(std::span<std::span<float>> input,
                  std::span<std::span<float>> output) {
    for (size_t ch = 0; ch < config_.channels; ch++) {
      processChannel(input[ch], output[ch], buffer_[ch], pos_);
    }
    pos_ = (pos_ + config_.block_size) % frames_;
    return StatusOk;
  }

  enum status GetOutputFrameRate(size_t* frame_rate) {
    *frame_rate = config_.frame_rate;
    return StatusOk;
  }

 private:
  void processChannel(std::span<float> input,
                      std::span<float> output,
                      std::vector<float>& buffer,
                      size_t pos) {
    for (size_t i = 0; i < config_.block_size; i++) {
      output[i] = std::clamp(buffer[pos] + input[i], -1.0f, 1.0f);
      buffer[pos] = output[i] * kEchoDecayMultlipier;
      pos = (pos + 1) % frames_;
    }
  }

 private:
  struct plugin_processor_config config_;
  size_t pos_ = 0;
  size_t frames_;
  // Echo buffer.
  std::vector<std::vector<float>> buffer_;
};

template <class T>
class CppWrapper {
 public:
  static CppWrapper* Create(const struct plugin_processor_config& config) {
    return new CppWrapper(config);
  }

  // This function implements plugin_processor->run.
  enum status Run(const struct multi_slice& input, struct multi_slice& output) {
    std::vector<std::span<float>> input_span;
    for (size_t ch = 0; ch < input.channels; ch++) {
      input_span.push_back(std::span(input.data[ch], input.num_frames));
    }
    std::vector<std::span<float>> output_span;
    for (auto& output_channel : output_buffer_) {
      output_span.push_back(std::span(output_channel));
    }

    wrapped_->Run(input_span, output_span);

    output.channels = output_span.size();
    output.num_frames = std::numeric_limits<size_t>::max();
    for (size_t ch = 0; ch < output_span.size(); ch++) {
      output.num_frames = std::min(output.num_frames, output_span[ch].size());
      output.data[ch] = output_span[ch].data();
    }

    return StatusOk;
  }

  enum status GetOutputFrameRate(size_t* frame_rate) {
    return wrapped_->GetOutputFrameRate(frame_rate);
  }

  ~CppWrapper() { delete wrapped_; }

  CppWrapper(const CppWrapper&) = delete;
  CppWrapper& operator=(const CppWrapper&) = delete;
  CppWrapper(CppWrapper&&) = delete;
  CppWrapper& operator=(CppWrapper&&) = delete;

 private:
  explicit CppWrapper(const struct plugin_processor_config& config)
      : wrapped_(new T(config)) {
    output_buffer_.resize(config.channels);
    for (size_t ch = 0; ch < config.channels; ch++) {
      output_buffer_[ch].resize(config.block_size);
    }
  }

 public:
  // All members are public to make CppWrapper<T> a standard layout type.
  // Which allows casting between T* and plugin_processor*.
  struct plugin_processor plugin_;

  T* wrapped_;
  std::vector<std::vector<float>> output_buffer_;
};

// Assert that casting between the embedded plugin_ and the
// CppWrapper<EchoProcessor> is valid.
static_assert(std::is_standard_layout_v<CppWrapper<EchoProcessor>>);
static_assert(offsetof(CppWrapper<EchoProcessor>, plugin_) == 0);

static enum status run(struct plugin_processor* p,
                       const struct multi_slice* input,
                       struct multi_slice* output) {
  auto wrapper = reinterpret_cast<CppWrapper<EchoProcessor>*>(p);
  wrapper->Run(*input, *output);
  return StatusOk;
}

static enum status destroy(struct plugin_processor* p) {
  delete reinterpret_cast<CppWrapper<EchoProcessor>*>(p);
  return StatusOk;
}

static enum status get_output_frame_rate(struct plugin_processor* p,
                                         size_t* frame_rate) {
  if (!p) {
    return ErrInvalidProcessor;
  }

  auto wrapper = reinterpret_cast<CppWrapper<EchoProcessor>*>(p);
  return wrapper->GetOutputFrameRate(frame_rate);
}

// plugin_processor ops for DenoiserWrapper.
static const struct plugin_processor_ops ops = {
    .run = run,
    .destroy = destroy,
    .get_output_frame_rate = get_output_frame_rate,
};

extern "C" enum status echo_processor_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config) {
  CppWrapper<EchoProcessor>* wrapper =
      CppWrapper<EchoProcessor>::Create(*config);
  wrapper->plugin_.ops = &ops;
  *out = reinterpret_cast<struct plugin_processor*>(wrapper);

  return StatusOk;
}

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_AUDIO_PROCESSOR_C_PLUGIN_PROCESSOR_H_
#define CRAS_SRC_AUDIO_PROCESSOR_C_PLUGIN_PROCESSOR_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MULTI_SLICE_MAX_CH 8

enum status {
  StatusOk,
  ErrOutOfMemory,
  ErrInvalidProcessor,
  ErrInvalidConfig,
  ErrInvalidArgument,
  ErrOther,
};

// A plugin audio processor. ops is the table for run and destroy functions.
struct plugin_processor {
  const struct plugin_processor_ops* ops;
};

// Configuration for plugin_processor.
struct plugin_processor_config {
  size_t channels;    // Number of input channels.
  size_t block_size;  // Number of input audio frames passed in each iteration.
  size_t frame_rate;  // Number of input frames in each second.
  bool debug;         // Whether to show debug information.
};

// Reference to multiple slices. Can be used to represent deinterelved audio
// data.
struct multi_slice {
  size_t channels;                  // Number of channels.
  size_t num_frames;                // Number of samples in each channel.
  float* data[MULTI_SLICE_MAX_CH];  // Pointers to the start of each channel.
};

// Create a plugin audio processor. The created processor should be stored in
// out. On error a status other than StatusOk should be returned.
//
// This is a C-style constructor for use in dlopen(3).
typedef enum status (*processor_create)(
    struct plugin_processor** out,
    const struct plugin_processor_config* config);

// Method table for plugin_processor. All functions are required.
struct plugin_processor_ops {
  // Run the plugin audio processor p. The plugin processor should store the
  // result in output.
  enum status (*run)(struct plugin_processor* p,
                     const struct multi_slice* input,
                     struct multi_slice* output);

  // Destruct the plugin audio processor p.
  enum status (*destroy)(struct plugin_processor* p);
};

#ifdef __cplusplus
}
#endif

#endif  // PLUGIN_PROCESSOR_H_

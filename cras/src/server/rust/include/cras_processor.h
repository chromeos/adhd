// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_PROCESSOR_H_
#define CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_PROCESSOR_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "audio_processor/c/plugin_processor.h"

enum CrasProcessorEffect {
  NoEffects,
  Negate,
  NoiseCancellation,
  StyleTransfer,
  Overridden,
};

struct CrasProcessorCreateResult {
  /**
   * The created processor.
   */
  struct plugin_processor *plugin_processor;
  /**
   * The actual effect used in the processor.
   * Might be different from what was passed to cras_processor_create.
   */
  enum CrasProcessorEffect effect;
};

struct CrasProcessorConfig {
  size_t channels;
  size_t block_size;
  size_t frame_rate;
  enum CrasProcessorEffect effect;
  bool dedicated_thread;
};

/**
 * Create a CRAS processor.
 *
 * Returns the created processor (might be NULL), and the applied effect.
 *
 * # Safety
 *
 * `config` must point to a CrasProcessorConfig struct.
 * `apm_plugin_processor` must point to a plugin_processor.
 */
struct CrasProcessorCreateResult cras_processor_create(const struct CrasProcessorConfig *config,
                                                       struct plugin_processor *apm_plugin_processor);

/**
 * Returns true if override is enabled in the system config file.
 */
bool cras_processor_is_override_enabled(void);

#endif /* CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_PROCESSOR_H_ */

#ifdef __cplusplus
}
#endif

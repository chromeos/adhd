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
#include <stdint.h>
#include <stdlib.h>

enum CrasProcessorEffect {
  NoEffects,
  Negate,
  NoiseCancellation,
};

struct CrasProcessorConfig {
  uintptr_t channels;
  uintptr_t block_size;
  uintptr_t frame_rate;
  enum CrasProcessorEffect effect;
};

/**
 * Create a CRAS processor.
 *
 * # Safety
 *
 * `config` must point to a CrasProcessorConfig struct.
 */
struct plugin_processor *cras_processor_create(const struct CrasProcessorConfig *config);

#endif /* CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_PROCESSOR_H_ */

#ifdef __cplusplus
}
#endif

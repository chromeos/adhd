// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SERVER_PROCESSOR_PROCESSOR_H_
#define CRAS_SERVER_PROCESSOR_PROCESSOR_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "audio_processor/c/plugin_processor.h"
#include "cras/common/rust_common.h"

enum CrasProcessorWrapMode {
  WrapModeNone,
  /**
   * Run the processor pipeline in a separate, dedicated thread.
   */
  WrapModeDedicatedThread,
  /**
   * Run the processor pipeline with a ChunkWrapper with the inner block
   * size set to  [`CrasProcessorConfig::block_size`].
   * In this mode, the caller is allowed to run the pipeline with a block
   * size that is different from  [`CrasProcessorConfig::block_size`].
   */
  WrapModeChunk,
  /**
   * Like `WrapModeChunk` but the pipeline is run inside a peer processor (sandbox).
   * [`CrasProcessorConfig::max_block_size`] must be set in this mode.
   * WAVE dump is not supported in this mode.
   */
  WrapModePeerChunk,
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
  enum CrasProcessorWrapMode wrap_mode;
  bool wav_dump;
  /**
   * The max block size when wrap_mode is WrapModePeerChunk.
   * Used to determine buffer size to allocate for peer IPC.
   */
  size_t max_block_size;
};

/**
 * Create a CRAS processor.
 *
 * Returns the created processor (might be NULL), and the applied effect.
 *
 * # Safety
 *
 * `config` must point to a CrasProcessorConfig struct.
 * `apm_plugin_processor` must point to a plugin_processor or NULL.
 */
struct CrasProcessorCreateResult cras_processor_create(const struct CrasProcessorConfig *config,
                                                       struct plugin_processor *apm_plugin_processor);

/**
 * Returns true if override is enabled in the system config file.
 */
bool cras_processor_is_override_enabled(void);

#endif  /* CRAS_SERVER_PROCESSOR_PROCESSOR_H_ */

#ifdef __cplusplus
}
#endif

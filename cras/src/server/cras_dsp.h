/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_DSP_H_
#define CRAS_SRC_SERVER_CRAS_DSP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "cras/src/server/cras_dsp_offload.h"
#include "cras/src/server/cras_dsp_pipeline.h"

struct cras_dsp_context;

/* Starts the dsp subsystem. It starts a thread internally to load the
 * plugins. This should be called before other functions.
 * Args:
 *    filename - The ini file where the dsp plugin graph should be read from.
 */
void cras_dsp_init(const char* filename);

// Stops the dsp subsystem.
void cras_dsp_stop();

/* Creates a dsp context. The context holds a pipeline and its
 * parameters.  To use the pipeline in the context, first use
 * cras_dsp_load_pipeline() to load it and then use
 * cras_dsp_get_pipeline() to lock it for access.
 * Args:
 *    sample_rate - The sampling rate of the pipeline.
 *    purpose - The purpose of the pipeline, "playback" or "capture".
 * Returns:
 *    A pointer to the dsp context.
 */
struct cras_dsp_context* cras_dsp_context_new(int sample_rate,
                                              const char* purpose);

// Sets the reference pointer of the offload map object to the context.
void cras_dsp_context_set_offload_map(struct cras_dsp_context* ctx,
                                      struct dsp_offload_map* offload_map);

// Frees a dsp context.
void cras_dsp_context_free(struct cras_dsp_context* ctx);

// Sets a configuration string variable in the context.
void cras_dsp_set_variable_string(struct cras_dsp_context* ctx,
                                  const char* key,
                                  const char* value);

// Sets a configuration boolean variable in the context.
void cras_dsp_set_variable_boolean(struct cras_dsp_context* ctx,
                                   const char* key,
                                   char value);

// Sets a configuration integer variable in the context.
void cras_dsp_set_variable_integer(struct cras_dsp_context* ctx,
                                   const char* key,
                                   int value);

/* Loads the pipeline to the context. This should be called again when
 * new values of configuration variables may change the plugin
 * graph. The actual loading happens in another thread to avoid
 * blocking the audio thread. */
void cras_dsp_load_pipeline(struct cras_dsp_context* ctx);

/* Loads a mock pipeline of source directly connects to sink, of given
 * number of channels.
 */
void cras_dsp_load_mock_pipeline(struct cras_dsp_context* ctx,
                                 unsigned int num_channels);

/* Locks the pipeline in the context for access. Returns NULL if the
 * pipeline is still being loaded or cannot be loaded. */
struct pipeline* cras_dsp_get_pipeline(struct cras_dsp_context* ctx);

/* Releases the pipeline in the context. This must be called in pair
 * with cras_dsp_get_pipeline() once the client finishes using the
 * pipeline. This should be called in the same thread as
 * cras_dsp_get_pipeline() was called. */
void cras_dsp_put_pipeline(struct cras_dsp_context* ctx);

/* Readapts the pipeline in the context. In contrast with
 * cras_dsp_load_pipeline:
 *  - is called while opening iodev, or updating active_node, i.e. at the start
 *    of audio playback on a certain node.
 *  - will replace the current cras_dsp_pipeline with a new one.
 *    ext_dsp_pipeline and swap_lr will be reset after replacement.
 *  - the offload process (mixer control config setting) can be done without
 *    blocking audio thread.
 * cras_dsp_readapt_pipeline:
 *  - is called while triggering DSP offload fallback for a running pipeline,
 *    i.e. in the middle of audio playback on a certain node.
 *  - will retain the current cras_dsp_pipeline. ext_dsp_pipeline and swap_lr
 *    are unaffected by readaptation.
 *  - the offload process will be done with audio thread blocked. As a
 *    consequence, this should be called only if necessary.
 */
void cras_dsp_readapt_pipeline(struct cras_dsp_context* ctx);

// Re-reads the ini file and reloads all pipelines in the system.
void cras_dsp_reload_ini();

// Dump current dsp information to syslog.
void cras_dsp_dump_info();

// Returns the active AP effects in the pipeline modules of the ctx, and returns
// 0 if none or ctx is NULL.
CRAS_STREAM_ACTIVE_AP_EFFECT cras_dsp_get_active_ap_effects(
    struct cras_dsp_context* ctx);

// Number of channels output.
unsigned int cras_dsp_num_output_channels(const struct cras_dsp_context* ctx);

// Number of channels input.
unsigned int cras_dsp_num_input_channels(const struct cras_dsp_context* ctx);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_DSP_H_

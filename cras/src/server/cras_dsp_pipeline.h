/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_DSP_PIPELINE_H_
#define CRAS_SRC_SERVER_CRAS_DSP_PIPELINE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "cras/src/common/cras_types_internal.h"
#include "cras/src/common/dumper.h"
#include "cras/src/server/cras_dsp_ini.h"
#include "cras/src/server/cras_dsp_module.h"
#include "cras/src/server/cras_dsp_offload.h"
#include "cras_audio_format.h"

/* These are the functions to create and use dsp pipelines. A dsp
 * pipeline is a collection of dsp plugins that process audio
 * data. The plugins and their connections are specified in an ini
 * file. Before using the pipeline, we need to instantiate the
 * pipeline by giving an audio sampling rate. Then we get the pointers
 * to the input buffers, fill the input data, run the pipeline, and
 * obtain the processed data from the output buffers.
 */

/* The maximum number of samples that cras_dsp_pipeline_run() can
 * accept. Beyond this the user should break the samples into several
 * blocks and call cras_dsp_pipeline_run() several times.
 */
#define DSP_BUFFER_SIZE 2048

struct pipeline;

/* Creates a pipeline from the given ini file.
 * Args:
 *    ini - The ini file the pipeline is created from.
 *    env - The expression environment for evaluating disable expression.
 *    purpose - The purpose of the pipeline, "playback" or "capture".
 * Returns:
 *    A pointer to the pipeline, or NULL if the creation fails.
 */
struct pipeline* cras_dsp_pipeline_create(struct ini* ini,
                                          struct cras_expr_env* env,
                                          const char* purpose);

// Frees the resources used by the pipeline.
void cras_dsp_pipeline_free(struct pipeline* pipeline);

/* Loads the implementation of the plugins in the pipeline (from
 * shared libraries). Must be called before
 * cras_dsp_pipeline_instantiate().
 * Returns:
 *    0 if successful. -1 otherwise.
 */
int cras_dsp_pipeline_load(struct pipeline* pipeline);

/* Instantiates the pipeline given the sampling rate.
 * Args:
 *    sample_rate - The audio sampling rate.
 *    env         - The expression environment.
 * Returns:
 *    0 if successful. -1 otherwise.
 */
int cras_dsp_pipeline_instantiate(struct pipeline* pipeline,
                                  int sample_rate,
                                  struct cras_expr_env* env);

/* The counterpart of cras_dsp_pipeline_instantiate(). To change the
 * sampling rate, this must be called before another call to
 * cras_dsp_pipeline_instantiate(). */
void cras_dsp_pipeline_deinstantiate(struct pipeline* pipeline);

/* Returns the buffering delay of the pipeline. This should only be called
 * after a pipeline has been instantiated.
 * Returns:
 *    The buffering delay in frames.
 */
int cras_dsp_pipeline_get_delay(struct pipeline* pipeline);

// Returns the number of input/output audio channels this pipeline expects
int cras_dsp_pipeline_get_num_input_channels(struct pipeline* pipeline);
int cras_dsp_pipeline_get_num_output_channels(struct pipeline* pipeline);

/* Returns the pointer to the input buffer for a channel of this
 * pipeline. The size of the buffer is DSP_BUFFER_SIZE samples, and
 * the number of samples actually used should be passed to
 * cras_dsp_pipeline_run().
 *
 * Args:
 *    index - The channel index. The valid value is 0 to
 *            cras_dsp_pipeline_get_num_channels() - 1.
 */
float* cras_dsp_pipeline_get_source_buffer(struct pipeline* pipeline,
                                           int index);

/* Returns the pointer to the output buffer for a channel of this
 * pipeline. The size of the buffer is DSP_BUFFER_SIZE samples.
 *
 * Args:
 *    index - The channel index. The valid value is 0 to
 *            cras_dsp_pipeline_get_num_channels() - 1.
 */
float* cras_dsp_pipeline_get_sink_buffer(struct pipeline* pipeline, int index);

/*
 * Connects |ext_module| to the sink of given dsp pipeline.
 * Args:
 *    pipeline - The pipeline whose sink should connect to ext_module.
 *    ext_module - The external dsp module to connect to pipeline sink.
 */
void cras_dsp_pipeline_set_sink_ext_module(struct pipeline* pipeline,
                                           struct ext_dsp_module* ext_module);

/*
 * Sets the flag of swapping L/R channel to the sink of given dsp pipeline.
 * Note it relies on clients to make sure that the swap L/R setting is only
 * requested on pipelines with 2-channel sink.
 * Args:
 *    pipeline - The pipeline whose sink should set the swap.
 *    left_right_swapped - Whether to swap L/R channel data.
 */
void cras_dsp_pipeline_set_sink_lr_swapped(struct pipeline* pipeline,
                                           bool left_right_swapped);

/*
 * Sets the applied flag of given CRAS DSP pipeline.
 * Args:
 *    pipeline - The pipeline which should set the offloaded flag.
 *    applied - True when DSP offload is applied; False otherwise.
 */
void cras_dsp_pipeline_apply_offload(struct pipeline* pipeline, bool applied);

/* Returns the number of internal audio buffers allocated by the
 * pipeline. This is used by the unit test only */
int cras_dsp_pipeline_get_peak_audio_buffers(struct pipeline* pipeline);

/* Returns the sampling rate passed by cras_dsp_pipeline_instantiate(),
 * or 0 if is has not been called */
int cras_dsp_pipeline_get_sample_rate(struct pipeline* pipeline);

// Gets the dsp ini that corresponds to the pipeline.
struct ini* cras_dsp_pipeline_get_ini(struct pipeline* pipeline);

/* Gets the string of DSP pattern for the pipeline. The returned DSP
 * pattern will be formed by DSP module labels concatenated with ">",
 * e.g. "drc>eq2".
 */
char* cras_dsp_pipeline_get_pattern(const struct pipeline* pipeline);

/* Runs the offload process for the pipeline by configuring the offload blobs
 * to the DSP mixer controls for each module.
 */
int cras_dsp_pipeline_config_offload(struct dsp_offload_map* offload_map,
                                     struct pipeline* pipeline);

/* Processes a block of audio samples. sample_count should be no more
 * than DSP_BUFFER_SIZE */
int cras_dsp_pipeline_run(struct pipeline* pipeline, int sample_count);

/* Add a statistic of running time for the pipeline.
 *
 * Args:
 *    time_delta - The time it takes to run the pipeline and any other
 *                 preprocessing and postprocessing.
 *    samples - The number of audio sample frames processed.
 */
void cras_dsp_pipeline_add_statistic(struct pipeline* pipeline,
                                     const struct timespec* time_delta,
                                     int samples);

/* Runs the specified pipeline across the given interleaved buffer in place.
 * Args:
 *    pipeline - The pipeline to run.
 *    buf - The samples to be processed, interleaved.
 *    format - Sample format of the buffer.
 *    frames - the number of samples in the buffer.
 * Returns:
 *    Negative code if error, otherwise 0.
 */
int cras_dsp_pipeline_apply(struct pipeline* pipeline,
                            uint8_t* buf,
                            snd_pcm_format_t format,
                            unsigned int frames);

/* Validate the specified pipeline matches the given hardware format
 * Args:
 *    pipeline - The pipeline to run.
 *    format - Sample format of the hardware.
 * Returns:
 *    Negative code if error, otherwise 0.
 */
int cras_dsp_pipeline_validate(const struct pipeline* pipeline,
                               const struct cras_audio_format* format);

// Dumps the current state of the pipeline. For debugging only
void cras_dsp_pipeline_dump(struct dumper* d, struct pipeline* pipeline);

// Returns the active AP effects in the pipeline modules, and returns 0 if none
// or pipeline is NULL.
CRAS_STREAM_ACTIVE_AP_EFFECT cras_dsp_pipeline_get_active_ap_effects(
    const struct pipeline* pipeline);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_DSP_PIPELINE_H_

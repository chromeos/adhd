/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_DSP_PIPELINE_H_
#define CRAS_DSP_PIPELINE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "dumper.h"
#include "cras_dsp_ini.h"

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
struct pipeline *cras_dsp_pipeline_create(struct ini* ini,
					  struct cras_expr_env *env,
					  const char *purpose);

/* Frees the resources used by the pipeline. */
void cras_dsp_pipeline_free(struct pipeline *pipeline);

/* Loads the implementation of the plugins in the pipeline (from
 * shared libraries). Must be called before
 * cras_dsp_pipeline_instantiate().
 * Returns:
 *    0 if successful. -1 otherwise.
 */
int cras_dsp_pipeline_load(struct pipeline *pipeline);

/* Instantiates the pipeline given the sampling rate.
 * Args:
 *    sample_rate - The audio sampling rate.
 * Returns:
 *    0 if successful. -1 otherwise.
 */
int cras_dsp_pipeline_instantiate(struct pipeline *pipeline, int sample_rate);

/* The counterpart of cras_dsp_pipeline_instantiate(). To change the
 * sampling rate, this must be called before another call to
 * cras_dsp_pipeline_instantiate(). */
void cras_dsp_pipeline_deinstantiate(struct pipeline *pipeline);

/* Returns the number of input/output audio channels this pipeline expects */
int cras_dsp_pipeline_get_num_channels(struct pipeline *pipeline);

/* Returns the pointer to the input buffer for a channel of this
 * pipeline. The size of the buffer is DSP_BUFFER_SIZE samples, and
 * the number of samples acually used should be passed to
 * cras_dsp_pipeline_run().
 *
 * Args:
 *    index - The channel index. The valid value is 0 to
 *            cras_dsp_pipeline_get_num_channels() - 1.
 */
float *cras_dsp_pipeline_get_source_buffer(struct pipeline *pipeline,
					   int index);

/* Returns the pointer to the output buffer for a channel of this
 * pipeline. The size of the buffer is DSP_BUFFER_SIZE samples.
 *
 * Args:
 *    index - The channel index. The valid value is 0 to
 *            cras_dsp_pipeline_get_num_channels() - 1.
 */
float *cras_dsp_pipeline_get_sink_buffer(struct pipeline *pipeline, int index);

/* Returns the number of internal audio buffers allocated by the
 * pipeline. This is used by the unit test only */
int cras_dsp_pipeline_get_peak_audio_buffers(struct pipeline *pipeline);

/* Returns the sampling rate passed by cras_dsp_pipeline_instantiate(),
 * or 0 if is has not been called */
int cras_dsp_pipeline_get_sample_rate(struct pipeline *pipeline);

/* Processes a block of audio samples. sample_count should be no more
 * than DSP_BUFFER_SIZE */
void cras_dsp_pipeline_run(struct pipeline *pipeline, int sample_count);

/* Dumps the current state of the pipeline. For debugging only */
void cras_dsp_pipeline_dump(struct dumper *d, struct pipeline *pipeline);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CRAS_DSP_PIPELINE_H_ */

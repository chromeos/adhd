/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Super Resolution, SR in brief, helps reconstruct high frequency
 * part of audio signal.
 *
 * This struct contains information needed for applying the SR algorithm.
 */

#ifndef CRAS_SR_H_
#define CRAS_SR_H_
#define CRAS_SR_MODEL_PATH_CAPACITY (256)

#include <stddef.h>

#include "byte_buffer.h"

struct cras_sr;

/* Cras SR model specification.
 *  model_path - The path to the tflite model.
 *  num_frames_per_run - number of frames needed by each invocation.
 *  num_channels - number of channels needed by each invocation.
 *  input_sample_rate - the input sample rate of the audio data.
 *  output_sample_rate - the output sample rate of the audio data.
 */
struct cras_sr_model_spec {
	char model_path[CRAS_SR_MODEL_PATH_CAPACITY + 1];
	size_t num_frames_per_run;
	size_t num_channels;
	size_t input_sample_rate;
	size_t output_sample_rate;
};

/* Creates a sr component.
 * Args:
 *    spec - the spec of the bt sr model.
 *    input_buf_size - The size of the input_buf. This is used as a reference to
 *      calculate the size of the internal buffers.
 */
struct cras_sr *cras_sr_create(struct cras_sr_model_spec spec,
			       size_t input_nbytes);

/* Destroys a sr component.
 * Args:
 *    sr - The sr to destroy.
 */
void cras_sr_destroy(struct cras_sr *sr);

/* Processes the input_buf and stores the results into output_buf.
 * Args:
 *     sr - The sr object.
 *     input_buf - the buffer that stores the input data to be processed.
 *     output_buf - the buffer that is used to store the processed data.
 * Returns:
 *     number of frames taken from input_buf.
 */
int cras_sr_process(struct cras_sr *sr, struct byte_buffer *input_buf,
		    struct byte_buffer *output_buf);

#endif /* CRAS_SR_H_ */

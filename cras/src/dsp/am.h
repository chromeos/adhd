/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_DSP_AM_H_
#define CRAS_SRC_DSP_AM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The context of an audio model, am in brief.
struct am_context;

// Creates an am context.
struct am_context* am_new(const char* model_path);

// Frees the am context.
void am_free(struct am_context* am_context);

/* Invokes the audio model given inputs and stores the results to outputs.
 *
 * Args:
 *    am_context - The am_context.
 *    inputs - The array of input audio samples.
 *    num_inputs - The number of elements in the inputs array to process.
 *    outputs - The array to store the output audio samples.
 *    num_outputs - The number of elements of the output array.
 * Returns:
 *    0 on success, otherwise an error number with syslog describing the error.
 */
int am_process(struct am_context* am_context,
               const float* inputs,
               const size_t num_inputs,
               float* outputs,
               const size_t num_outputs);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_DSP_AM_H_

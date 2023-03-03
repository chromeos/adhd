/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_DSP_DCBLOCK_H_
#define CRAS_SRC_DSP_DCBLOCK_H_

#ifdef __cplusplus
extern "C" {
#endif

// A DC blocking filter.

struct dcblock;

// Create a DC blocking filter.
struct dcblock* dcblock_new();

// Free a DC blocking filter.
void dcblock_free(struct dcblock* dcblock);

/*
 * Configure a DC blocking filter.
 *
 * Transfer fn: (1 - z^-1) / (1 - R * z^-1)
 * Args:
 *    R - DC block filter coefficient.
 *    sample_rate - The sample rate, in Hz.
 */
void dcblock_set_config(struct dcblock* dcblock,
                        float R,
                        unsigned long sample_rate);

/* Process a buffer of audio data through the filter.
 * Args:
 *    dcblock - The filter we want to use.
 *    data - The array of audio samples.
 *    count - The number of elements in the data array to process.
 */
void dcblock_process(struct dcblock* dcblock, float* data, int count);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_DSP_DCBLOCK_H_

/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_DSP_DSP_UTIL_H_
#define CRAS_SRC_DSP_DSP_UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "cras_audio_format.h"

/* Converts from interleaved int16_t samples to non-interleaved float samples.
 * The int16_t samples have range [-32768, 32767], and the float samples have
 * range [-1.0, 1.0].
 * Args:
 *    input - The interleaved input buffer. Every "channels" samples is a frame.
 *    output - Pointers to output buffers. There are "channels" output buffers.
 *    channels - The number of samples per frame.
 *    frames - The number of frames to convert.
 * Returns:
 *    Negative error if format isn't supported, otherwise 0.
 */
int dsp_util_deinterleave(uint8_t* input,
                          float* const* output,
                          int channels,
                          snd_pcm_format_t format,
                          int frames);

/* Converts from non-interleaved float samples to interleaved int16_t samples.
 * The int16_t samples have range [-32768, 32767], and the float samples have
 * range [-1.0, 1.0]. This is the inverse of dsputil_deinterleave().
 * Args:
 *    input - Pointers to input buffers. There are "channels" input buffers.
 *    output - The interleaved output buffer. Every "channels" samples is a
 *        frame.
 *    channels - The number of samples per frame.
 *    frames - The number of frames to convert.
 * Returns:
 *    Negative error if format isn't supported, otherwise 0.
 */
int dsp_util_interleave(float* const* input,
                        uint8_t* output,
                        int channels,
                        snd_pcm_format_t format,
                        int frames);

/* Disables denormal numbers in floating point calculation. Denormal numbers
 * happens often in IIR filters, and it can be very slow.
 */
void dsp_enable_flush_denormal_to_zero();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // DSPUTIL_H_

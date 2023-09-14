/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * DSP Helpers - Keeps handy functions commonly used among dsp domain.
 */

#ifndef CRAS_SRC_DSP_DSP_HELPERS_H_
#define CRAS_SRC_DSP_DSP_HELPERS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Converts a float number to the fixed Qnx.ny format. The parameter 'ny' is the
 * fraction bit length while 'nx' (= 32 - 'ny') stands for the integer bit
 * length excluding one for sign. Variables in Q-format are accommodated in
 * int32_t while one should keep 'ny' noted. The example is as follows:
 *
 * int32_t qx; // as Q4.28
 *    --> ny = 28, nx = 4
 *    --> consist of 1 sign + 3 integer + 28 fraction bits
 *    --> the lowest bit has scalar = 2^-28, the next bit = 2^-27, and so on as
 *        BCD system
 *    --> the value range of qx: [-2^3, 2^3) in 2's complement
 * Args:
 *    f - The input float number to convert.
 *    ny - The fraction bit length, must be 31 or lower.
 * Returns:
 *    The Q-formatted variable stored in int32_t.
 */
static inline int32_t float_to_qint32(float f, int ny) {
  return (int32_t)(((const double)f) * ((int64_t)1 << (const int)ny) + 0.5);
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // DSP_HELPERS_H_

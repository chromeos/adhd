/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_DSP_TESTS_DSP_TEST_UTIL_H_
#define CRAS_SRC_DSP_TESTS_DSP_TEST_UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Tests if the system supports denormal numbers. Returns 1 if so, 0
 * otherwise.*/
int dsp_util_has_denormal();

// Clears floating point exceptions. For debugging only.
void dsp_util_clear_fp_exceptions();

// Prints floating point exceptions to stdout. For debugging only.
void dsp_util_print_fp_exceptions();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_DSP_TESTS_DSP_TEST_UTIL_H_

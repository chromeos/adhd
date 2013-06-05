/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DSPUTIL_H_
#define DSPUTIL_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Disables denormal numbers in floating point calculation. Denormal numbers
 * happens often in IIR filters, and it can be very slow.
 */
void dsp_enable_flush_denormal_to_zero();

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DSPUTIL_H_ */

/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SOFTVOL_CURVE_H_
#define SOFTVOL_CURVE_H_

struct cras_volume_curve;

extern const float softvol_scalers[101];

/* Returns the volume scaler in the soft volume curve for the given index. */
static inline float softvol_get_scaler(unsigned int volume_index)
{
	return softvol_scalers[volume_index];
}

/* Builds software volume scalers from volume curve. */
float *softvol_build_from_curve(const struct cras_volume_curve *curve);

#endif /* SOFTVOL_CURVE_H_ */

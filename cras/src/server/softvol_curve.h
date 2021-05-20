/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SOFTVOL_CURVE_H_
#define SOFTVOL_CURVE_H_

#include <math.h>

#define LOG_10 2.302585

struct cras_volume_curve;

extern const float softvol_scalers[101];

/* Returns the volume scaler in the soft volume curve for the given index. */
static inline float softvol_get_scaler(unsigned int volume_index)
{
	return softvol_scalers[volume_index];
}

/*
 * Converts input_node_gain [0, 100] to dBFS.
 * Linear maps [0, 50) to [-4000, 0) and [50, 100] to [0, 2000] dBFS.
 */
static inline long convert_dBFS_from_input_node_gain(long gain)
{
	/* Assert value in range 0 - 100. */
	if (gain < 0)
		gain = 0;
	if (gain > 100)
		gain = 100;
	const long db_scale = (gain > 50) ? 40 : 80;
	return (gain - 50) * db_scale;
}

/* The inverse function of convert_dBFS_from_input_node_gain. */
static inline long convert_input_node_gain_from_dBFS(long dBFS)
{
	return 50 + dBFS / ((dBFS > 0) ? 40 : 80);
}

/* convert dBFS to softvol scaler */
static inline float convert_softvol_scaler_from_dB(long dBFS)
{
	return expf(LOG_10 * dBFS / 2000);
}

/* The inverse function of convert_softvol_scaler_from_dB. */
static inline long convert_dBFS_from_softvol_scaler(float scaler)
{
	/*
	 * Use lround here instead of direct casting to long to prevent
	 * incorrect inversion.
	 */
	return lround(logf(scaler) / LOG_10 * 2000);
}

/* Builds software volume scalers from volume curve. */
float *softvol_build_from_curve(const struct cras_volume_curve *curve);

#endif /* SOFTVOL_CURVE_H_ */

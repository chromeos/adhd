/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stddef.h>
#include <stdlib.h>

#include "cras_volume_curve.h"

/* Default to 1dB per tick.
 * Volume = 100 -> 0dB.
 * Volume = 0 -> -100dB. */
static long get_dBFS_default(const struct cras_volume_curve *curve,
			     size_t volume)
{
	/* dB to cut * 100 */
	return (volume - 100) * 100;
}

/* Simple curve with configurable max volume and volume step. */
struct stepped_curve {
	struct cras_volume_curve curve;
	long max_vol;
	long step;
};

static long get_dBFS_step(const struct cras_volume_curve *curve, size_t volume)
{
	const struct stepped_curve *c = (const struct stepped_curve *)curve;
	return c->max_vol - (c->step * (100 - volume));
}

/*
 * Exported Interface.
 */

struct cras_volume_curve *cras_volume_curve_create_default()
{
	struct cras_volume_curve *curve;
	curve = (struct cras_volume_curve *)calloc(1, sizeof(*curve));
	if (curve != NULL)
		curve->get_dBFS = get_dBFS_default;
	return curve;
}

struct cras_volume_curve *cras_volume_curve_create_simple_step(
		long max_volume,
		long volume_step)
{
	struct stepped_curve *curve;
	curve = (struct stepped_curve *)calloc(1, sizeof(*curve));
	if (curve == NULL)
		return NULL;
	curve->curve.get_dBFS = get_dBFS_step;
	curve->max_vol = max_volume;
	curve->step = volume_step;
	return &curve->curve;
}

void cras_volume_curve_destroy(struct cras_volume_curve *curve)
{
	free(curve);
}

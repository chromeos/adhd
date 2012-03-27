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

void cras_volume_curve_destroy(struct cras_volume_curve *curve)
{
	free(curve);
}

/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stddef.h>

/* Default to 1dB per tick.
 * Volume = 100 -> 0dB.
 * Volume = 0 -> -100dB. */
long cras_volume_curve_get_db_for_index(size_t volume)
{
	/* dB to cut * 100 */
	return (volume - 100) * 100;
}

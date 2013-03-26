/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

extern const float softvol_scalers[101];

/* Returns the volume scaler in the soft volume curve for the given index. */
static inline float softvol_get_scaler(unsigned int volume_index)
{
	return softvol_scalers[volume_index];
}

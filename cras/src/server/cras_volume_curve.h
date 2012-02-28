/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_VOLUME_CURVE_H_
#define CRAS_VOLUME_CURVE_H_

/* Converts a volume index into a dB level.
 * Args:
 *    volume - the volume level from 0 to 100.
 * Returns:
 *    The volume to apply in dB * 100.  This value will normally be negative and
 *    is means dB down form full scale.
 */
long cras_volume_curve_get_dBFS_for_index(size_t volume);

#endif /* CRAS_VOLUME_CURVE_H_ */

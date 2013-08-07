/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CRAS_MIX_H
#define _CRAS_MIX_H

struct cras_audio_shm;

/* Renders count frames from shm into dst.  Updates count if anything is
 * written.  If the system is muted, this will render zeros to the output.
 * Args:
 *    shm - Area to mix samples from.
 *    num_channel - Number of channels in data.
 *    dst - Output buffer.  Add samples to this.
 *    count - The number of samples to render, on return holds the number
 *        actually mixed.
 *    index - The index of the stream.  This will be incremented after mixing.
 */
size_t cras_mix_add_stream(struct cras_audio_shm *shm,
			   size_t num_channels,
			   uint8_t *dst,
			   size_t *count,
			   size_t *index);

/* Scale the given buffer with the provided scaler.
 * Args:
 *    buffer - Buffer of samples to scale.
 *    scaler - Amount to scale samples (0.0 - 1.0).
 *    count - The number of samples to render, on return holds the number
 *        actually mixed.
 */
void cras_scale_buffer(int16_t *buffer, unsigned int count, float scaler);

#endif /* _CRAS_MIX_H */

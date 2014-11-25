/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CRAS_MIX_H
#define _CRAS_MIX_H

struct cras_audio_shm;

/* Scale the given buffer with the provided scaler.
 * Args:
 *    fmt - The format (SND_PCM_FORMAT_*)
 *    buff - Buffer of samples to scale.
 *    scaler - Amount to scale samples (0.0 - 1.0).
 *    count - The number of samples to render, on return holds the number
 *        actually mixed.
 */
void cras_scale_buffer(snd_pcm_format_t fmt, uint8_t *buff, unsigned int count,
		       float scaler);

/* Add src buffer to dst, scaling and setting mute.
 * Args:
 *    fmt - The format (SND_PCM_FORMAT_*)
 *    dst - Buffer of samples to mix to.
 *    src - Buffer of samples to mix from.
 *    count - The number of samples to mix.
 *    index - If zero this is the first buffer written to dst.
 *    mute - Is the stream providing the buffer muted.
 *    mix_vol - Scaler for the buffer to be mixed.
 */
void cras_mix_add(snd_pcm_format_t fmt, uint8_t *dst, uint8_t *src,
		  unsigned int count, unsigned int index,
		  int mute, float mix_vol);

/* Mutes the given buffer.
 * Args:
 *    num_channel - Number of channels in data.
 *    frame_bytes - number of bytes in a frame.
 *    count - The number of frames to render.
 */
size_t cras_mix_mute_buffer(uint8_t *dst,
			    size_t frame_bytes,
			    size_t count);

#endif /* _CRAS_MIX_H */

/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Alsa Helpers - Keeps the interface to alsa localized to this file.
 */

#ifndef _CRAS_ALSA_HELPERS_H
#define _CRAS_ALSA_HELPERS_H

#include <alsa/asoundlib.h>
#include <stdint.h>
#include <stdlib.h>

struct cras_audio_format;

/* Opens an alsa device, thin wrapper to snd_pcm_open.
 * Args:
 *    handle - Filled with a pointer to the opened pcm.
 *    dev - Path to the alsa device to test.
 *    stream - Alsa stream type, input or output.
 * Returns:
 *    See docs for snd_pcm_open.
 */
int cras_alsa_pcm_open(snd_pcm_t **handle, const char *dev,
		       snd_pcm_stream_t stream);

/* Closes an alsa device, thin wrapper to snd_pcm_close.
 * Args:
 *    handle - Filled with a pointer to the opened pcm.
 * Returns:
 *    See docs for snd_pcm_close.
 */
int cras_alsa_pcm_close(snd_pcm_t *handle);

/* Starts an alsa device, thin wrapper to snd_pcm_start.
 * Args:
 *    handle - Filled with a pointer to the opened pcm.
 * Returns:
 *    See docs for snd_pcm_start.
 */
int cras_alsa_pcm_start(snd_pcm_t *handle);

/* Drains an alsa device, thin wrapper to snd_pcm_drain.
 * Args:
 *    handle - Filled with a pointer to the opened pcm.
 * Returns:
 *    See docs for snd_pcm_drain.
 */
int cras_alsa_pcm_drain(snd_pcm_t *handle);

/* Checks what sample rates are supported.
 * Args:
 *    dev - Path to the alsa device to test.
 *    stream - Alsa stream type, input or output.
 * Returns:
 *    A pointer to the list of supported sample rates.  This list is NULL
 *    terminated, and must be freed by the caller when he's done with it.
 *    NULL is returned on error.
 */
size_t *cras_alsa_check_sample_rates(const char *dev, snd_pcm_stream_t stream);

/* Sets up the hwparams to alsa.
 * Args:
 *    handle - The open PCM to configure.
 *    format - The audio format desired for playback/capture.
 *    buffer_frames - Number of frames in the ALSA buffer.
 * Returns:
 *    0 on success, negative error on failure.
 */
int cras_alsa_set_hwparams(snd_pcm_t *handle, struct cras_audio_format *format,
			   snd_pcm_uframes_t *buffer_frames);

/* Sets up the swparams to alsa.
 * Args:
 *    handle - The open PCM to configure.
 * Returns:
 *    0 on success, negative error on failure.
 */
int cras_alsa_set_swparams(snd_pcm_t *handle);

/* Get the number of used frames in the alsa buffer.
 * Args:
 *    handle - The open PCM to configure.
 *    buf_size - Number of frames in the ALSA buffer.
 *    used - Filled with the number of used frames.
 * Returns:
 *    0 on success, negative error on failure.
 */
int cras_alsa_get_avail_frames(snd_pcm_t *handle, snd_pcm_uframes_t buf_size,
			       snd_pcm_uframes_t *used);

/* Get the current alsa delay, make sure it's no bigger than the buffer size.
 * Args:
 *    handle - The open PCM to configure.
 *    buf_size - Number of frames in the ALSA buffer.
 *    delay - Filled with the number of delay frames.
 * Returns:
 *    0 on success, negative error on failure.
 */
int cras_alsa_get_delay_frames(snd_pcm_t *handle, snd_pcm_uframes_t buf_size,
			       snd_pcm_sframes_t *delay);

/* Wrapper for snd_pcm_mmap_begin
 * Args:
 *    handle - The open PCM to configure.
 *    format_bytes - Number of bytes in a single frame.
 *    dst - Pointer set to the area for reading/writing the audio.
 *    offset - Filled with the offset to pass back to commit.
 *    frames - Passed with the max number of frames to request. Filled with the
 *        max number to use.
 *    underruns - counter to increment if an under-run occurs.
 * Returns:
 *    zero on success, negative error code for fatal
 *    errors.
 */
int cras_alsa_mmap_begin(snd_pcm_t *handle, size_t format_bytes,
			 uint8_t **dst, snd_pcm_uframes_t *offset,
			 snd_pcm_uframes_t *frames, size_t *underruns);

/* Wrapper for snd_pcm_mmap_commit
 * Args:
 *    handle - The open PCM to configure.
 *    offset - offset from call to mmap_begin.
 *    frames - # of frames written/read.
 *    underruns - counter to increment if an under-run occurs.
 * Returns:
 *    zero on success, negative error code for fatal
 *    errors.
 */
int cras_alsa_mmap_commit(snd_pcm_t *handle, snd_pcm_uframes_t offset,
			  snd_pcm_uframes_t frames, size_t *underruns);

#endif /* _CRAS_ALSA_HELPERS_H */

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

/* Probes properties of the alsa device.
 * Args:
 *    dev - Path to the alsa device to test.
 *    stream - Alsa stream type, input or output.
 *    rates - Pointer that will be set to the arrary of valid samples rates.
 *            Must be freed by the caller.
 *    channel_counts - Pointer that will be set to the array of valid channel
 *                     counts.  Must be freed by the caller.
 * Returns:
 *   0 on success.  On failure an error code from alsa or -ENOMEM.
 */
int cras_alsa_fill_properties(const char *dev, snd_pcm_stream_t stream,
			      size_t **rates, size_t **channel_counts);

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
int cras_alsa_mmap_begin(snd_pcm_t *handle, unsigned int format_bytes,
			 uint8_t **dst, snd_pcm_uframes_t *offset,
			 snd_pcm_uframes_t *frames, unsigned int *underruns);

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
			  snd_pcm_uframes_t frames, unsigned int *underruns);

/* When the stream is suspended, due to a system suspend, loop until we can
 * resume it. Won't actually loop very much because the system will be
 * suspended.
 * Args:
 *    handle - The open PCM to configure.
 * Returns:
 *    zero on success, negative error code for fatal
 *    errors.
 */
int cras_alsa_attempt_resume(snd_pcm_t *handle);

#endif /* _CRAS_ALSA_HELPERS_H */

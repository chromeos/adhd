/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SHM_H_
#define CRAS_SHM_H_

#include <assert.h>
#include <stdint.h>

#include "cras_types.h"
#include "cras_util.h"

#define CRAS_NUM_SHM_BUFFERS 2U /* double buffer */
#define CRAS_SHM_BUFFERS_MASK (CRAS_NUM_SHM_BUFFERS - 1)

/* Structure that tracks memory shared between server and client.
 *
 *  read_buf_idx - index of the current buffer to read from (0 or 1 if double
 *    buffered).
 *  write_buf_idx - index of the current buffer to write to (0 or 1 if double
 *    buffered).
 *  read_offset - offset of the next sample to read (one per buffer).
 *  write_offset - offset of the next sample to write (one per buffer).
 *  size - The size of the samples area in bytes.
 *  used_size - The size in bytes of the sample area being actively used.
 *  volume_scaler - volume scaling factor (0.0-1.0).
 *  muted - bool, true if stream should be muted.
 *  num_overruns - Starting at 0 this is incremented very time data is over
 *    written because too much accumulated before a read.
 *  num_cb_timeouts = how many times has the client failed to meet the read or
 *    write deadline.
 *  ts - For capture, the time stamp of the next sample at read_index.  For
 *    playback, this is the time that the next sample written will be played.
 *    This is only valid in audio callbacks.
 *  samples - Audio data - a double buffered area that is used to exchange
 *    audio samples.
 */
struct cras_audio_shm_area {
	size_t read_buf_idx; /* use buffer A or B */
	size_t write_buf_idx;
	size_t read_offset[CRAS_NUM_SHM_BUFFERS];
	size_t write_offset[CRAS_NUM_SHM_BUFFERS];
	size_t frame_bytes;
	size_t size;
	size_t used_size;
	float volume_scaler;
	size_t mute;
	size_t callback_pending;
	size_t num_overruns;
	size_t num_cb_timeouts;
	struct timespec ts;
	uint8_t samples[];
};

/* Get a pointer to the buffer at idx. */
static inline uint8_t *cras_shm_buff_for_idx(struct cras_audio_shm_area *shm,
					     size_t idx)
{
	return shm->samples + shm->used_size * idx;
}

/* Get a pointer to the current read buffer */
static inline uint8_t *cras_shm_get_curr_read_buffer(
		struct cras_audio_shm_area *shm)
{
	return cras_shm_buff_for_idx(shm, shm->read_buf_idx) +
		shm->read_offset[shm->read_buf_idx];
}

/* Get a pointer to the next buffer to write */
static inline uint8_t *cras_shm_get_curr_write_buffer(
		struct cras_audio_shm_area *shm)
{
	return cras_shm_buff_for_idx(shm, shm->write_buf_idx) +
		shm->write_offset[shm->write_buf_idx];
}

/* Get a pointer to the current read buffer plus an offset.  The offset might be
 * in the next buffer. This returns the number of frames you can memcpy out of
 * shm. */
static inline int16_t *cras_shm_get_readable_frames(
		struct cras_audio_shm_area *shm, size_t offset,
		size_t *frames)
{
	size_t buf_idx = shm->read_buf_idx;
	size_t final_offset;

	assert(frames != NULL);
	final_offset = shm->read_offset[buf_idx] + offset * shm->frame_bytes;
	if (final_offset >= shm->write_offset[buf_idx]) {
		final_offset -= shm->write_offset[buf_idx];
		assert_on_compile_is_power_of_2(CRAS_NUM_SHM_BUFFERS);
		buf_idx = (buf_idx + 1) & CRAS_SHM_BUFFERS_MASK;
	}
	if (final_offset >= shm->write_offset[buf_idx])
		return 0;
	*frames = (shm->write_offset[buf_idx] - final_offset) /
			shm->frame_bytes;
	return (int16_t *)(cras_shm_buff_for_idx(shm, buf_idx) + final_offset);
}

/* How many bytes are queued? */
static inline size_t cras_shm_get_bytes_queued(struct cras_audio_shm_area *shm)
{
	size_t total, i;

	total = 0;
	for (i = 0; i < CRAS_NUM_SHM_BUFFERS; i++)
		total += shm->write_offset[i] - shm->read_offset[i];
	return total;
}

/* How many frames are queued? */
static inline size_t cras_shm_get_frames(struct cras_audio_shm_area *shm)
{
	size_t bytes;

	bytes = cras_shm_get_bytes_queued(shm);
	assert(bytes % shm->frame_bytes == 0);
	return bytes / shm->frame_bytes;
}

/* How many frames in the current buffer? */
static inline size_t cras_shm_get_frames_in_curr_buffer(
		struct cras_audio_shm_area *shm)
{
	size_t buf_idx = shm->read_buf_idx;
	size_t bytes;

	bytes = shm->write_offset[buf_idx] - shm->read_offset[buf_idx];
	assert(bytes % shm->frame_bytes == 0);
	return bytes / shm->frame_bytes;
}

/* How many are available to be written? */
static inline size_t cras_shm_get_avail_curr_buffer(
		struct cras_audio_shm_area *shm)
{
	size_t buf_idx = shm->read_buf_idx;
	size_t bytes;

	bytes = shm->write_offset[buf_idx] - shm->read_offset[buf_idx];
	assert(bytes % shm->frame_bytes == 0);
	return (shm->used_size - bytes) / shm->frame_bytes;
}

/* Return 1 if there is an empty buffer in the list. */
static inline int cras_shm_is_buffer_available(struct cras_audio_shm_area *shm)
{
	size_t buf_idx = shm->write_buf_idx;

	return (shm->write_offset[buf_idx] == 0);
}

/* Increment the write pointer for the current buffer. */
static inline void cras_shm_buffer_written(struct cras_audio_shm_area *shm,
					   size_t frames)
{
	size_t buf_idx = shm->write_buf_idx;

	if (shm->write_offset[buf_idx])
		shm->num_overruns++; /* Should only write to empty buffers */
	shm->write_offset[buf_idx] = frames * shm->frame_bytes;
	shm->read_offset[buf_idx] = 0;
	/* And move on to the next buffer. */
	assert_on_compile_is_power_of_2(CRAS_NUM_SHM_BUFFERS);
	buf_idx = (buf_idx + 1) & CRAS_SHM_BUFFERS_MASK;
	shm->write_buf_idx = buf_idx;
}

/* Increment the read pointer.  If it goes past the write pointer for this
 * buffer, move to the next buffer. */
static inline void cras_shm_buffer_read(struct cras_audio_shm_area *shm,
					size_t frames)
{
	size_t buf_idx = shm->read_buf_idx;
	size_t remainder;

	shm->read_offset[buf_idx] += frames * shm->frame_bytes;
	if (shm->read_offset[buf_idx] >= shm->write_offset[buf_idx]) {
		remainder = shm->read_offset[buf_idx] -
				shm->write_offset[buf_idx];
		shm->read_offset[buf_idx] = shm->write_offset[buf_idx] = 0;
		assert_on_compile_is_power_of_2(CRAS_NUM_SHM_BUFFERS);
		buf_idx = (buf_idx + 1) & CRAS_SHM_BUFFERS_MASK;
		if (remainder < shm->write_offset[buf_idx])
			shm->read_offset[buf_idx] = remainder;
		else {
			shm->read_offset[buf_idx] = 0;
			shm->write_offset[buf_idx] = 0;
		}
		shm->read_buf_idx = buf_idx;
	}
}

/* Sets the volume for the stream.  The volume level is a scaling factor that
 * will be applied to the stream before mixing. */
static inline void cras_shm_set_volume_scaler(struct cras_audio_shm_area *shm,
					      float volume_scaler)
{
	volume_scaler = max(volume_scaler, 0.0);
	shm->volume_scaler = min(volume_scaler, 1.0);
}

/* Returns the volume of the stream(0.0-1.0). */
static inline float cras_shm_get_volume_scaler(struct cras_audio_shm_area *shm)
{
	return shm->volume_scaler;
}

/* Indicates that the stream should be muted/unmuted */
static inline void cras_shm_set_mute(struct cras_audio_shm_area *shm,
				     size_t mute)
{
	shm->mute = !!mute;
}

/* Returns the mute state of the stream.  0 if not muted, non-zero if muted. */
static inline size_t cras_shm_get_mute(struct cras_audio_shm_area *shm)
{
	return shm->mute;
}

#endif /* CRAS_SHM_H_ */

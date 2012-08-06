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
	assert_on_compile_is_power_of_2(CRAS_NUM_SHM_BUFFERS);
	idx = idx & CRAS_SHM_BUFFERS_MASK;
	return shm->samples + shm->used_size * idx;
}

/* Get a pointer to the current read buffer */
static inline uint8_t *cras_shm_get_curr_read_buffer(
		struct cras_audio_shm_area *shm)
{
	unsigned i = shm->read_buf_idx & CRAS_SHM_BUFFERS_MASK;
	return cras_shm_buff_for_idx(shm, i) + shm->read_offset[i];
}

/* Get a pointer to the next buffer to write */
static inline uint8_t *cras_shm_get_curr_write_buffer(
		struct cras_audio_shm_area *shm)
{
	unsigned i = shm->write_buf_idx & CRAS_SHM_BUFFERS_MASK;
	return cras_shm_buff_for_idx(shm, i) + shm->write_offset[i];
}

/* Get a pointer to the current read buffer plus an offset.  The offset might be
 * in the next buffer. This returns the number of frames you can memcpy out of
 * shm. */
static inline int16_t *cras_shm_get_readable_frames(
		struct cras_audio_shm_area *shm, size_t offset,
		size_t *frames)
{
	unsigned buf_idx = shm->read_buf_idx & CRAS_SHM_BUFFERS_MASK;
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
	size_t buf_idx = shm->read_buf_idx & CRAS_SHM_BUFFERS_MASK;
	size_t bytes;

	bytes = shm->write_offset[buf_idx] - shm->read_offset[buf_idx];
	assert(bytes % shm->frame_bytes == 0);
	return bytes / shm->frame_bytes;
}

/* Return 1 if there is an empty buffer in the list. */
static inline int cras_shm_is_buffer_available(struct cras_audio_shm_area *shm)
{
	size_t buf_idx = shm->write_buf_idx & CRAS_SHM_BUFFERS_MASK;

	return (shm->write_offset[buf_idx] == 0);
}

/* How many are available to be written? */
static inline size_t cras_shm_get_num_writeable(
		struct cras_audio_shm_area *shm)
{
	/* Not allowed to write to a buffer twice. */
	if (!cras_shm_is_buffer_available(shm))
		return 0;

	return shm->used_size / shm->frame_bytes;
}

/* Flags an overrun if writing would cause one. */
static inline void cras_shm_check_write_overrun(struct cras_audio_shm_area *shm)
{
	size_t buf_idx = shm->write_buf_idx & CRAS_SHM_BUFFERS_MASK;

	if (shm->write_offset[buf_idx])
		shm->num_overruns++; /* Should only write to empty buffers */
	shm->write_offset[buf_idx] = 0;
}

/* Increment the write pointer for the current buffer. */
static inline void cras_shm_buffer_written(struct cras_audio_shm_area *shm,
					   size_t frames)
{
	size_t buf_idx = shm->write_buf_idx & CRAS_SHM_BUFFERS_MASK;

	shm->write_offset[buf_idx] += frames * shm->frame_bytes;
	shm->read_offset[buf_idx] = 0;
}

/* Signals the writing to this buffer is complete and moves to the next one. */
static inline void cras_shm_buffer_write_complete(
		struct cras_audio_shm_area *shm)
{
	size_t buf_idx = shm->write_buf_idx & CRAS_SHM_BUFFERS_MASK;

	assert_on_compile_is_power_of_2(CRAS_NUM_SHM_BUFFERS);
	buf_idx = (buf_idx + 1) & CRAS_SHM_BUFFERS_MASK;
	shm->write_buf_idx = buf_idx;
}

/* Increment the read pointer.  If it goes past the write pointer for this
 * buffer, move to the next buffer. */
static inline void cras_shm_buffer_read(struct cras_audio_shm_area *shm,
					size_t frames)
{
	size_t buf_idx = shm->read_buf_idx & CRAS_SHM_BUFFERS_MASK;
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

/* Sets the size of a frame in bytes. */
static inline void cras_shm_set_frame_bytes(struct cras_audio_shm_area *shm,
					    unsigned frame_bytes)
{
	shm->frame_bytes = frame_bytes;
}

/* Returns the size of a frame in bytes. */
static inline
unsigned cras_shm_frame_bytes(const struct cras_audio_shm_area *shm)
{
	return shm->frame_bytes;
}

/* Sets if a callback is pending with the client. */
static inline
void cras_shm_set_callback_pending(struct cras_audio_shm_area *shm, int pending)
{
	shm->callback_pending = !!pending;
}

/* Returns non-zero if a callback is pending for this shm region. */
static inline
int cras_shm_callback_pending(const struct cras_audio_shm_area *shm)
{
	return shm->callback_pending;
}

/* Sets the used_size of the shm region.  This is the maximum number of bytes
 * that is exchanged each time a buffer is passed from client to server.
 */
static inline
void cras_shm_set_used_size(struct cras_audio_shm_area *shm, unsigned used_size)
{
	shm->used_size = used_size;
}

/* Returns the used size of the shm region in bytes. */
static inline unsigned cras_shm_used_size(const struct cras_audio_shm_area *shm)
{
	return shm->used_size;
}

/* Returns the used size of the shm region in frames. */
static inline
unsigned cras_shm_used_frames(const struct cras_audio_shm_area *shm)
{
	return shm->used_size / shm->frame_bytes;
}

#endif /* CRAS_SHM_H_ */

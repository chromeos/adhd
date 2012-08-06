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

/* Configuration of the shm area.
 *
 *  used_size - The size in bytes of the sample area being actively used.
 *  frame_bytes - The size of each frame in bytes.
 */
struct cras_audio_shm_config {
	unsigned int used_size;
	unsigned int frame_bytes;
};

/* Structure that is shared as shm between client and server.
 *
 *  config - Size config data.  A copy of the config shared with clients.
 *  read_buf_idx - index of the current buffer to read from (0 or 1 if double
 *    buffered).
 *  write_buf_idx - index of the current buffer to write to (0 or 1 if double
 *    buffered).
 *  read_offset - offset of the next sample to read (one per buffer).
 *  write_offset - offset of the next sample to write (one per buffer).
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
	struct cras_audio_shm_config config;
	size_t read_buf_idx; /* use buffer A or B */
	size_t write_buf_idx;
	size_t read_offset[CRAS_NUM_SHM_BUFFERS];
	size_t write_offset[CRAS_NUM_SHM_BUFFERS];
	float volume_scaler;
	size_t mute;
	size_t callback_pending;
	size_t num_overruns;
	size_t num_cb_timeouts;
	struct timespec ts;
	uint8_t samples[];
};

/* Structure that holds the config for and a pointer to the audio shm area.
 *
 *  config - Size config data, kept separate so it can be checked.
 *  area - Acutal shm region that is shared.
 */
struct cras_audio_shm {
	struct cras_audio_shm_config config;
	struct cras_audio_shm_area *area;
};

/* Get a pointer to the buffer at idx. */
static inline uint8_t *cras_shm_buff_for_idx(struct cras_audio_shm *shm,
					     size_t idx)
{
	assert_on_compile_is_power_of_2(CRAS_NUM_SHM_BUFFERS);
	idx = idx & CRAS_SHM_BUFFERS_MASK;
	return shm->area->samples + shm->config.used_size * idx;
}

/* Get a pointer to the current read buffer */
static inline uint8_t *cras_shm_get_curr_read_buffer(struct cras_audio_shm *shm)
{
	unsigned i = shm->area->read_buf_idx & CRAS_SHM_BUFFERS_MASK;
	return cras_shm_buff_for_idx(shm, i) + shm->area->read_offset[i];
}

/* Get a pointer to the next buffer to write */
static inline
uint8_t *cras_shm_get_curr_write_buffer(struct cras_audio_shm *shm)
{
	unsigned i = shm->area->write_buf_idx & CRAS_SHM_BUFFERS_MASK;
	return cras_shm_buff_for_idx(shm, i) + shm->area->write_offset[i];
}

/* Get a pointer to the current read buffer plus an offset.  The offset might be
 * in the next buffer. This returns the number of frames you can memcpy out of
 * shm. */
static inline int16_t *cras_shm_get_readable_frames(struct cras_audio_shm *shm,
						    size_t offset,
						    size_t *frames)
{
	unsigned buf_idx = shm->area->read_buf_idx & CRAS_SHM_BUFFERS_MASK;
	size_t final_offset;

	assert(frames != NULL);
	final_offset = shm->area->read_offset[buf_idx] + offset *
		       shm->config.frame_bytes;
	if (final_offset >= shm->area->write_offset[buf_idx]) {
		final_offset -= shm->area->write_offset[buf_idx];
		assert_on_compile_is_power_of_2(CRAS_NUM_SHM_BUFFERS);
		buf_idx = (buf_idx + 1) & CRAS_SHM_BUFFERS_MASK;
	}
	if (final_offset >= shm->area->write_offset[buf_idx])
		return 0;
	*frames = (shm->area->write_offset[buf_idx] - final_offset) /
			shm->config.frame_bytes;
	return (int16_t *)(cras_shm_buff_for_idx(shm, buf_idx) + final_offset);
}

/* How many bytes are queued? */
static inline size_t cras_shm_get_bytes_queued(struct cras_audio_shm *shm)
{
	size_t total, i;

	total = 0;
	for (i = 0; i < CRAS_NUM_SHM_BUFFERS; i++)
		total += shm->area->write_offset[i] - shm->area->read_offset[i];
	return total;
}

/* How many frames are queued? */
static inline size_t cras_shm_get_frames(struct cras_audio_shm *shm)
{
	size_t bytes;

	bytes = cras_shm_get_bytes_queued(shm);
	assert(bytes % shm->config.frame_bytes == 0);
	return bytes / shm->config.frame_bytes;
}

/* How many frames in the current buffer? */
static inline
size_t cras_shm_get_frames_in_curr_buffer(struct cras_audio_shm *shm)
{
	size_t buf_idx = shm->area->read_buf_idx & CRAS_SHM_BUFFERS_MASK;
	size_t bytes;
	struct cras_audio_shm_area *area = shm->area;

	bytes = area->write_offset[buf_idx] - area->read_offset[buf_idx];
	return bytes / shm->config.frame_bytes;
}

/* Return 1 if there is an empty buffer in the list. */
static inline int cras_shm_is_buffer_available(struct cras_audio_shm *shm)
{
	size_t buf_idx = shm->area->write_buf_idx & CRAS_SHM_BUFFERS_MASK;

	return (shm->area->write_offset[buf_idx] == 0);
}

/* How many are available to be written? */
static inline size_t cras_shm_get_num_writeable(struct cras_audio_shm *shm)
{
	/* Not allowed to write to a buffer twice. */
	if (!cras_shm_is_buffer_available(shm))
		return 0;

	return shm->config.used_size / shm->config.frame_bytes;
}

/* Flags an overrun if writing would cause one. */
static inline void cras_shm_check_write_overrun(struct cras_audio_shm *shm)
{
	size_t buf_idx = shm->area->write_buf_idx & CRAS_SHM_BUFFERS_MASK;

	if (shm->area->write_offset[buf_idx])
		shm->area->num_overruns++; /* Only write to empty buffers */
	shm->area->write_offset[buf_idx] = 0;
}

/* Increment the write pointer for the current buffer. */
static inline
void cras_shm_buffer_written(struct cras_audio_shm *shm, size_t frames)
{
	size_t buf_idx = shm->area->write_buf_idx & CRAS_SHM_BUFFERS_MASK;

	shm->area->write_offset[buf_idx] += frames * shm->config.frame_bytes;
	shm->area->read_offset[buf_idx] = 0;
}

/* Signals the writing to this buffer is complete and moves to the next one. */
static inline void cras_shm_buffer_write_complete(struct cras_audio_shm *shm)
{
	size_t buf_idx = shm->area->write_buf_idx & CRAS_SHM_BUFFERS_MASK;

	assert_on_compile_is_power_of_2(CRAS_NUM_SHM_BUFFERS);
	buf_idx = (buf_idx + 1) & CRAS_SHM_BUFFERS_MASK;
	shm->area->write_buf_idx = buf_idx;
}

/* Increment the read pointer.  If it goes past the write pointer for this
 * buffer, move to the next buffer. */
static inline
void cras_shm_buffer_read(struct cras_audio_shm *shm, size_t frames)
{
	size_t buf_idx = shm->area->read_buf_idx & CRAS_SHM_BUFFERS_MASK;
	size_t remainder;
	struct cras_audio_shm_area *area = shm->area;
	struct cras_audio_shm_config *config = &shm->config;

	area->read_offset[buf_idx] += frames * config->frame_bytes;
	if (area->read_offset[buf_idx] >= area->write_offset[buf_idx]) {
		remainder = area->read_offset[buf_idx] -
				area->write_offset[buf_idx];
		area->read_offset[buf_idx] = 0;
		area->write_offset[buf_idx] = 0;
		assert_on_compile_is_power_of_2(CRAS_NUM_SHM_BUFFERS);
		buf_idx = (buf_idx + 1) & CRAS_SHM_BUFFERS_MASK;
		if (remainder < area->write_offset[buf_idx]) {
			area->read_offset[buf_idx] = remainder;
		} else {
			area->read_offset[buf_idx] = 0;
			area->write_offset[buf_idx] = 0;
		}
		area->read_buf_idx = buf_idx;
	}
}

/* Sets the volume for the stream.  The volume level is a scaling factor that
 * will be applied to the stream before mixing. */
static inline
void cras_shm_set_volume_scaler(struct cras_audio_shm *shm, float volume_scaler)
{
	volume_scaler = max(volume_scaler, 0.0);
	shm->area->volume_scaler = min(volume_scaler, 1.0);
}

/* Returns the volume of the stream(0.0-1.0). */
static inline float cras_shm_get_volume_scaler(struct cras_audio_shm *shm)
{
	return shm->area->volume_scaler;
}

/* Indicates that the stream should be muted/unmuted */
static inline void cras_shm_set_mute(struct cras_audio_shm *shm, size_t mute)
{
	shm->area->mute = !!mute;
}

/* Returns the mute state of the stream.  0 if not muted, non-zero if muted. */
static inline size_t cras_shm_get_mute(struct cras_audio_shm *shm)
{
	return shm->area->mute;
}

/* Sets the size of a frame in bytes. */
static inline void cras_shm_set_frame_bytes(struct cras_audio_shm *shm,
					    unsigned frame_bytes)
{
	shm->config.frame_bytes = frame_bytes;
	if (shm->area)
		shm->area->config.frame_bytes = frame_bytes;
}

/* Returns the size of a frame in bytes. */
static inline unsigned cras_shm_frame_bytes(const struct cras_audio_shm *shm)
{
	return shm->config.frame_bytes;
}

/* Sets if a callback is pending with the client. */
static inline
void cras_shm_set_callback_pending(struct cras_audio_shm *shm, int pending)
{
	shm->area->callback_pending = !!pending;
}

/* Returns non-zero if a callback is pending for this shm region. */
static inline int cras_shm_callback_pending(const struct cras_audio_shm *shm)
{
	return shm->area->callback_pending;
}

/* Sets the used_size of the shm region.  This is the maximum number of bytes
 * that is exchanged each time a buffer is passed from client to server.
 */
static inline
void cras_shm_set_used_size(struct cras_audio_shm *shm, unsigned used_size)
{
	shm->config.used_size = used_size;
	if (shm->area)
		shm->area->config.used_size = used_size;
}

/* Returns the used size of the shm region in bytes. */
static inline unsigned cras_shm_used_size(const struct cras_audio_shm *shm)
{
	return shm->config.used_size;
}

/* Returns the used size of the shm region in frames. */
static inline unsigned cras_shm_used_frames(const struct cras_audio_shm *shm)
{
	return shm->config.used_size / shm->config.frame_bytes;
}

/* Returns the total size of the shared memory region. */
static inline unsigned cras_shm_total_size(const struct cras_audio_shm *shm)
{
	return cras_shm_used_size(shm) * CRAS_NUM_SHM_BUFFERS +
			sizeof(*shm->area);
}

/* Gets the counter of over-runs. */
static inline
unsigned cras_shm_num_overruns(const struct cras_audio_shm *shm)
{
	return shm->area->num_overruns;
}

/* Increments the counter of callback timeouts. */
static inline void cras_shm_inc_cb_timeouts(struct cras_audio_shm *shm)
{
	shm->area->num_cb_timeouts++;
}

/* Gets the counter of callback timeouts. */
static inline
unsigned cras_shm_num_cb_timeouts(const struct cras_audio_shm *shm)
{
	return shm->area->num_cb_timeouts;
}

/* Copy the config from the shm region to the local config.  Used by clients
 * when initially setting up the region.
 */
static inline void cras_shm_copy_shared_config(struct cras_audio_shm *shm)
{
	memcpy(&shm->config, &shm->area->config, sizeof(shm->config));
}

#endif /* CRAS_SHM_H_ */

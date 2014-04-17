/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <sys/param.h>
#include <syslog.h>

#include "cras_config.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_types.h"
#include "cras_util.h"
#include "utlist.h"

#define LOOPBACK_BUFFER_SIZE 8192

/* loopack iodev.  Keep state of a loopback device.
 *    buffer - The audio samples being looped.
 *    buffer_frames - Number of audio frames that fit in the buffer.
 *    read_offset - Current read pointer.
 *    write_offset - Current write pointer.
 *    open - Is the device open.
 *    pre_buffered - Has half of the buffer been filled once. Doesn't allow
 *        removing samples until buffered.
 */
struct loopback_iodev {
	struct cras_iodev base;
	uint8_t *buffer;
	unsigned int buffer_frames;
	unsigned int read_offset;
	unsigned int write_offset;
	int open;
	int pre_buffered;
};

/*
 * iodev callbacks.
 */

static int is_open(const struct cras_iodev *iodev)
{
	struct loopback_iodev *loopback_iodev = (struct loopback_iodev *)iodev;

	return loopback_iodev && loopback_iodev->open;
}

static int dev_running(const struct cras_iodev *iodev)
{
	return is_open(iodev);
}

static int frames_queued(const struct cras_iodev *iodev)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;

	if (!loopdev->pre_buffered)
		return 0;

	if (loopdev->write_offset > loopdev->read_offset)
		return loopdev->write_offset - loopdev->read_offset;

	return loopdev->write_offset +
	       (loopdev->buffer_frames - loopdev->read_offset);;
}

static int delay_frames(const struct cras_iodev *iodev)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;
	return loopdev->buffer_frames / 2;
}

static int close_dev(struct cras_iodev *iodev)
{
	struct loopback_iodev *loopback_iodev = (struct loopback_iodev *)iodev;

	loopback_iodev->open = 0;
	free(loopback_iodev->buffer);
	loopback_iodev->buffer = NULL;
	loopback_iodev->pre_buffered = 0;
	cras_iodev_free_format(iodev);
	return 0;
}

static int open_dev(struct cras_iodev *iodev)
{
	struct loopback_iodev *loopback_iodev = (struct loopback_iodev *)iodev;

	loopback_iodev->open = 1;

	loopback_iodev->buffer = malloc(cras_get_format_bytes(iodev->format) *
					LOOPBACK_BUFFER_SIZE);
	loopback_iodev->buffer_frames = LOOPBACK_BUFFER_SIZE;
	loopback_iodev->read_offset = 0;
	loopback_iodev->write_offset = 0;
	return 0;
}

static int get_buffer(struct cras_iodev *iodev, uint8_t **dst, unsigned *frames)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;
	unsigned int frame_bytes = cras_get_format_bytes(iodev->format);
	*dst = loopdev->buffer + loopdev->read_offset * frame_bytes;

	*frames = MIN(*frames, loopdev->buffer_frames - loopdev->read_offset);
	*frames = MIN(*frames, frames_queued(iodev));
	return 0;
}

static int put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;
	loopdev->read_offset += nwritten;
	if (loopdev->read_offset >= loopdev->buffer_frames)
		loopdev->read_offset = 0;
	return 0;
}

static void update_active_node(struct cras_iodev *iodev)
{
}

/*
 * Exported Interface.
 */

struct cras_iodev *loopback_iodev_create(enum CRAS_STREAM_DIRECTION direction)
{
	struct loopback_iodev *loopback_iodev;
	struct cras_iodev *iodev;

	if (direction != CRAS_STREAM_POST_MIX_PRE_DSP)
		return NULL;

	loopback_iodev = calloc(1, sizeof(*loopback_iodev));
	if (loopback_iodev == NULL)
		return NULL;
	iodev = &loopback_iodev->base;
	iodev->direction = direction;

	snprintf(iodev->info.name,
		 ARRAY_SIZE(iodev->info.name),
		 "Loopback record device.");
	iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';
	cras_iodev_list_add_input(iodev);

	iodev->supported_rates = calloc(2, sizeof(*iodev->supported_rates));
	iodev->supported_rates[0] = 44100;
	iodev->supported_channel_counts =
		calloc(2, sizeof(*iodev->supported_channel_counts));
	iodev->supported_channel_counts[0] = 2;
	iodev->buffer_size = LOOPBACK_BUFFER_SIZE;
	iodev->software_volume_scaler = 1.0;

	iodev->open_dev = open_dev;
	iodev->close_dev = close_dev;
	iodev->is_open = is_open;
	iodev->frames_queued = frames_queued;
	iodev->delay_frames = delay_frames;
	iodev->get_buffer = get_buffer;
	iodev->put_buffer = put_buffer;
	iodev->dev_running = dev_running;
	iodev->update_active_node = update_active_node;

	return iodev;
}

void loopback_iodev_destroy(struct cras_iodev *iodev)
{
	struct loopback_iodev *loopback_iodev = (struct loopback_iodev *)iodev;

	cras_iodev_list_rm_input(iodev);

	free(iodev->supported_rates);
	free(iodev->supported_channel_counts);
	free(loopback_iodev);
}

int loopback_iodev_add_audio(struct cras_iodev *dev,
			     const uint8_t *audio,
			     unsigned int count)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)dev;
	uint8_t *dst;
	unsigned int this_count;
	unsigned int frame_bytes;
	unsigned int total_written = 0;

	if (!is_open(dev))
		return 0;

	frame_bytes = cras_get_format_bytes(dev->format);

	/* copy samples to buffer accounting for wrap around. */
	dst = loopdev->buffer + loopdev->write_offset * frame_bytes;
	this_count = MIN(count, loopdev->buffer_frames - loopdev->write_offset);
	memcpy(dst, audio, this_count * frame_bytes);
	loopdev->write_offset += this_count;
	total_written = this_count;
	if (loopdev->write_offset >= loopdev->buffer_frames)
		loopdev->write_offset = 0;

	/* write any remaining frames after wrapping. */
	if (this_count < count) {
		this_count = count - this_count;
		memcpy(loopdev->buffer, audio + total_written * frame_bytes,
			this_count * frame_bytes);
		loopdev->write_offset += this_count;
	}

	if (loopdev->write_offset >= loopdev->buffer_frames / 2)
		loopdev->pre_buffered = 1;

	return 0;
}

int loopback_iodev_add_zeros(struct cras_iodev *dev,
			     unsigned int count)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)dev;
	uint8_t *dst;
	unsigned int this_count;
	unsigned int frame_bytes;

	if (!is_open(dev))
		return 0;

	frame_bytes = cras_get_format_bytes(dev->format);

	/* copy samples to buffer accounting for wrap around. */
	dst = loopdev->buffer + loopdev->write_offset * frame_bytes;
	this_count = MIN(count, loopdev->buffer_frames - loopdev->write_offset);
	memset(dst, 0, this_count * frame_bytes);
	loopdev->write_offset += this_count;
	if (loopdev->write_offset >= loopdev->buffer_frames)
		loopdev->write_offset = 0;

	/* write any remaining frames after wrapping. */
	if (this_count < count) {
		this_count = count - this_count;
		memset(loopdev->buffer, 0, this_count * frame_bytes);
		loopdev->write_offset += this_count;
	}

	if (loopdev->write_offset >= loopdev->buffer_frames / 2)
		loopdev->pre_buffered = 1;

	return 0;
}

void loopback_iodev_set_format(struct cras_iodev *iodev,
			       const struct cras_audio_format *fmt)
{
	iodev->supported_rates[0] = fmt->frame_rate;
	iodev->supported_channel_counts[0] = fmt->num_channels;
}

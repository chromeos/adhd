/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <syslog.h>

#include "cras_config.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_rstream.h"
#include "cras_types.h"
#include "utlist.h"

#define LOOPBACK_BUFFER_SIZE 8192

struct loopback_iodev {
	struct cras_iodev base;
	int open;
};

/*
 * iodev callbacks.
 */

static int is_open(const struct cras_iodev *iodev)
{
	struct loopback_iodev *loopback_iodev = (struct loopback_iodev *)iodev;

	return loopback_iodev->open;
}

static int dev_running(const struct cras_iodev *iodev)
{
	return 1;
}

static int frames_queued(const struct cras_iodev *iodev)
{
	return 0;
}

static int delay_frames(const struct cras_iodev *iodev)
{
	return 0;
}

static int close_dev(struct cras_iodev *iodev)
{
	struct loopback_iodev *loopback_iodev = (struct loopback_iodev *)iodev;

	loopback_iodev->open = 0;
	return 0;
}

static int open_dev(struct cras_iodev *iodev)
{
	struct loopback_iodev *loopback_iodev = (struct loopback_iodev *)iodev;

	loopback_iodev->open = 1;
	return 0;
}

static int get_buffer(struct cras_iodev *iodev, uint8_t **dst, unsigned *frames)
{
	return 0;
}

static int put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
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
			     unsigned int count,
			     struct cras_rstream *stream)
{
	struct cras_audio_shm *shm;
	unsigned int total_written;
	uint8_t *dst;
	int rc;
	unsigned int this_count;
	unsigned int max_loops;
	unsigned int cb_threshold;

	if (!cras_stream_is_loopback(stream->direction))
		return -EINVAL;

	shm = cras_rstream_input_shm(stream);
	cb_threshold = cras_rstream_get_cb_threshold(stream);

	total_written = 0;
	max_loops = 2;  /* Should never be hit, but just in case. */

	/* Fill up the stream with samples, this may only be able to
	 * write some of the samples to the current shm region, overflow
	 * to the next one on the subsequent loop. */
	while (total_written < count && max_loops--) {
		cras_shm_check_write_overrun(shm);
		dst = cras_shm_get_writeable_frames(
				shm,
				cb_threshold,
				&this_count);
		this_count = min(this_count, count - total_written);
		memcpy(dst,
		       audio + total_written * cras_shm_frame_bytes(shm),
		       this_count * cras_shm_frame_bytes(shm));
		cras_shm_buffer_written(shm, this_count);
		total_written += this_count;

		if (cras_shm_frames_written(shm) >= cb_threshold) {
			cras_shm_buffer_write_complete(shm);
			rc = cras_rstream_audio_ready(stream, cb_threshold);
			if (rc < 0)
				return rc;
		}
	}

	return 0;
}

void loopback_iodev_set_format(struct cras_iodev *iodev,
			       const struct cras_audio_format *fmt)
{
	iodev->supported_rates[0] = fmt->frame_rate;
	iodev->supported_channel_counts[0] = fmt->num_channels;
}

/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
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
#include "audio_thread.h"
#include "utlist.h"

static const size_t EMPTY_IODEV_PRIORITY = 0; /* lowest possible */
#define EMPTY_BUFFER_SIZE (48 * 1024)
#define EMPTY_FRAME_SIZE 4
#define EMPTY_FRAMES (EMPTY_BUFFER_SIZE / EMPTY_FRAME_SIZE)

static size_t empty_supported_rates[] = {
	44100, 48000, 0
};

static size_t empty_supported_channel_counts[] = {
	1, 2, 0
};

struct empty_iodev {
	struct cras_iodev base;
	int open;
	uint8_t audio_buffer[EMPTY_BUFFER_SIZE];
};

/*
 * iodev callbacks.
 */

static int is_open(const struct cras_iodev *iodev)
{
	struct empty_iodev *empty_iodev = (struct empty_iodev *)iodev;

	return empty_iodev->open;
}

static int dev_running(const struct cras_iodev *iodev)
{
	return 1;
}

static int frames_queued(const struct cras_iodev *iodev)
{
	if (iodev->direction == CRAS_STREAM_INPUT)
		return (int)iodev->cb_threshold;

	/* For output, return number of frames that are used. */
	return iodev->buffer_size - iodev->cb_threshold;
}

static int delay_frames(const struct cras_iodev *iodev)
{
	return 0;
}

static int close_dev(struct cras_iodev *iodev)
{
	struct empty_iodev *empty_iodev = (struct empty_iodev *)iodev;

	empty_iodev->open = 0;
	return 0;
}

static int open_dev(struct cras_iodev *iodev)
{
	struct empty_iodev *empty_iodev = (struct empty_iodev *)iodev;

	empty_iodev->open = 1;
	return 0;
}

static int get_buffer(struct cras_iodev *iodev, uint8_t **dst, unsigned *frames)
{
	struct empty_iodev *empty_iodev = (struct empty_iodev *)iodev;

	*dst = empty_iodev->audio_buffer;

	if (*frames > EMPTY_FRAMES)
		*frames = EMPTY_FRAMES;

	return 0;
}

static int put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	return 0;
}

/*
 * Exported Interface.
 */

struct cras_iodev *empty_iodev_create(enum CRAS_STREAM_DIRECTION direction)
{
	struct empty_iodev *empty_iodev;
	struct cras_iodev *iodev;

	empty_iodev = calloc(1, sizeof(*empty_iodev));
	if (empty_iodev == NULL)
		return NULL;
	iodev = &empty_iodev->base;

	iodev->info.priority = EMPTY_IODEV_PRIORITY;
	iodev->direction = direction;

	/* Finally add it to the appropriate iodev list. */
	if (direction == CRAS_STREAM_INPUT) {
		snprintf(iodev->info.name,
			 ARRAY_SIZE(iodev->info.name),
			 "Silent record device.");
		iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';
		cras_iodev_list_add_input(iodev);
	} else {
		assert(direction == CRAS_STREAM_OUTPUT);
		snprintf(iodev->info.name,
			 ARRAY_SIZE(iodev->info.name),
			 "Silent playback device.");
		iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';
		cras_iodev_list_add_output(iodev);
	}

	iodev->supported_rates = empty_supported_rates;
	iodev->supported_channel_counts = empty_supported_channel_counts;
	iodev->buffer_size = EMPTY_BUFFER_SIZE;

	iodev->open_dev = open_dev;
	iodev->close_dev = close_dev;
	iodev->is_open = is_open;
	iodev->frames_queued = frames_queued;
	iodev->delay_frames = delay_frames;
	iodev->get_buffer = get_buffer;
	iodev->put_buffer = put_buffer;
	iodev->dev_running = dev_running;

	iodev->thread = audio_thread_create(iodev);
	if (!iodev->thread) {
		syslog(LOG_ERR, "Failed to create empty iodev.");
		free(iodev);
		return NULL;
	}

	return iodev;
}

void empty_iodev_destroy(struct cras_iodev *iodev)
{
	struct empty_iodev *empty_iodev = (struct empty_iodev *)iodev;

	audio_thread_destroy(iodev->thread);
	if (iodev->direction == CRAS_STREAM_INPUT)
		cras_iodev_list_rm_input(iodev);
	else {
		assert(iodev->direction == CRAS_STREAM_OUTPUT);
		cras_iodev_list_rm_output(iodev);
	}
	free(empty_iodev);
}

/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <sys/param.h>
#include <syslog.h>

#include "cras_audio_area.h"
#include "cras_config.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_types.h"
#include "cras_util.h"
#include "utlist.h"

#define LOOPBACK_BUFFER_SIZE 8192

static size_t loopback_supported_rates[] = {
	44100, 0
};

static size_t loopback_supported_channel_counts[] = {
	2, 0
};

static snd_pcm_format_t loopback_supported_formats[] = {
	SND_PCM_FORMAT_S16_LE, 0
};

/* Shared buffer between loopback devices.
 *    buffer - The audio samples being looped.
 *    buffer_frames - Number of audio frames that fit in the buffer.
 *    read_offset - Current read pointer.
 *    write_offset - Current write pointer.
 *    write_ahead - True if write offset is ahead of read and wrapped.
 */
struct shared_buffer {
	uint8_t *buffer;
	unsigned int buffer_frames;
	unsigned int read_offset;
	unsigned int write_offset;
	int write_ahead;
};

/* loopack iodev.  Keep state of a loopback device.
 *    open - Is the device open.
 *    shared_buffer - Pointer to shared buffer between loopback devices.
 */
struct loopback_iodev {
	struct cras_iodev base;
	int open;
	struct shared_buffer *shared_buffer;
};

/*
 * iodev callbacks.
 */

static int is_open(const struct cras_iodev *iodev)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;

	return loopdev && loopdev->open;
}

static int dev_running(const struct cras_iodev *iodev)
{
	return is_open(iodev);
}

static int frames_queued(const struct cras_iodev *iodev)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;
	struct shared_buffer *sbuf = loopdev->shared_buffer;

	if (sbuf->write_ahead)
		return sbuf->write_offset +
		       (sbuf->buffer_frames - sbuf->read_offset);
	if (sbuf->write_offset > sbuf->read_offset)
		return sbuf->write_offset - sbuf->read_offset;
	return 0;
}

static int delay_frames(const struct cras_iodev *iodev)
{
	return frames_queued(iodev);
}

static int close_record_dev(struct cras_iodev *iodev)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;

	loopdev->open = 0;
	cras_iodev_free_format(iodev);
	cras_iodev_free_audio_area(iodev);
	return 0;
}

static int open_record_dev(struct cras_iodev *iodev)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;

	cras_iodev_init_audio_area(iodev, iodev->format->num_channels);
	loopdev->open = 1;
	return 0;
}

static int get_record_buffer(struct cras_iodev *iodev,
		      struct cras_audio_area **area,
		      unsigned *frames)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;
	struct shared_buffer *sbuf = loopdev->shared_buffer;
	unsigned int frame_bytes = cras_get_format_bytes(iodev->format);

	*frames = MIN(*frames, sbuf->buffer_frames - sbuf->read_offset);
	*frames = MIN(*frames, frames_queued(iodev));

	iodev->area->frames = *frames;
	cras_audio_area_config_buf_pointers(iodev->area, iodev->format,
			sbuf->buffer + sbuf->read_offset * frame_bytes);
	*area = iodev->area;
	return 0;
}

static int put_record_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;
	struct shared_buffer *sbuf = loopdev->shared_buffer;

	sbuf->read_offset += nwritten;
	if (sbuf->read_offset >= sbuf->buffer_frames) {
		sbuf->read_offset = 0;
		sbuf->write_ahead = 0;
	}
	return 0;
}

static int close_playback_dev(struct cras_iodev *iodev)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;
	struct shared_buffer *sbuf = loopdev->shared_buffer;

	loopdev->open = 0;
	cras_iodev_free_format(iodev);
	cras_iodev_free_audio_area(iodev);

	free(sbuf->buffer);
	sbuf->buffer = NULL;
	return 0;
}

static int open_playback_dev(struct cras_iodev *iodev)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;
	struct shared_buffer *sbuf = loopdev->shared_buffer;

	cras_iodev_init_audio_area(iodev, iodev->format->num_channels);
	loopdev->open = 1;

	sbuf->buffer = malloc(cras_get_format_bytes(iodev->format) *
			      LOOPBACK_BUFFER_SIZE);
	sbuf->buffer_frames = LOOPBACK_BUFFER_SIZE;
	sbuf->read_offset = 0;
	sbuf->write_offset = 0;
	sbuf->write_ahead = 0;
	return 0;
}

static int get_playback_buffer(struct cras_iodev *iodev,
		      struct cras_audio_area **area,
		      unsigned *frames)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;
	struct shared_buffer *sbuf = loopdev->shared_buffer;
	unsigned int frame_bytes = cras_get_format_bytes(iodev->format);

	*frames = MIN(*frames, sbuf->buffer_frames - sbuf->write_offset);
	*frames = MIN(*frames, sbuf->buffer_frames - frames_queued(iodev));

	iodev->area->frames = *frames;
	cras_audio_area_config_buf_pointers(iodev->area, iodev->format,
			sbuf->buffer + sbuf->write_offset * frame_bytes);
	*area = iodev->area;
	return 0;
}

static int put_playback_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	struct loopback_iodev *loopdev = (struct loopback_iodev *)iodev;
	struct shared_buffer *sbuf = loopdev->shared_buffer;

	sbuf->write_offset += nwritten;
	if (sbuf->write_offset >= sbuf->buffer_frames) {
		sbuf->write_offset = 0;
		sbuf->write_ahead = 1;
	}
	return 0;
}

static void update_active_node(struct cras_iodev *iodev)
{
}

static struct cras_iodev *create_loopback_iodev(enum CRAS_STREAM_DIRECTION dir,
						const char *name,
						struct shared_buffer *sbuf)
{
	struct loopback_iodev *loopback_iodev;
	struct cras_iodev *iodev;

	loopback_iodev = calloc(1, sizeof(*loopback_iodev));
	if (loopback_iodev == NULL)
		return NULL;

	loopback_iodev->shared_buffer = sbuf;

	iodev = &loopback_iodev->base;
	iodev->direction = dir;
	snprintf(iodev->info.name, ARRAY_SIZE(iodev->info.name), "%s", name);
	iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';

	iodev->supported_rates = loopback_supported_rates;
	iodev->supported_channel_counts = loopback_supported_channel_counts;
	iodev->supported_formats = loopback_supported_formats;
	iodev->buffer_size = LOOPBACK_BUFFER_SIZE;

	iodev->is_open = is_open;
	iodev->dev_running = dev_running;
	iodev->frames_queued = frames_queued;
	iodev->delay_frames = delay_frames;
	iodev->update_active_node = update_active_node;

	return iodev;
}

/*
 * Exported Interface.
 */

void loopback_iodev_create(struct cras_iodev **loopback_input,
			   struct cras_iodev **loopback_output)
{
	struct shared_buffer *sbuf;
	struct cras_iodev *iodev;

	sbuf = calloc(1, sizeof(*sbuf));
	if (sbuf == NULL)
		return;

	iodev = create_loopback_iodev(CRAS_STREAM_INPUT,
				      "Loopback record device.",
				      sbuf);
	if (iodev == NULL)
		return;
	iodev->info.idx = LOOPBACK_RECORD_DEVICE;
	iodev->open_dev = open_record_dev;
	iodev->close_dev = close_record_dev;
	iodev->get_buffer = get_record_buffer;
	iodev->put_buffer = put_record_buffer;
	*loopback_input = iodev;

	iodev = create_loopback_iodev(CRAS_STREAM_OUTPUT,
				      "Loopback playback device.",
				      sbuf);
	if (iodev == NULL)
		return;
	iodev->open_dev = open_playback_dev;
	iodev->close_dev = close_playback_dev;
	iodev->get_buffer = get_playback_buffer;
	iodev->put_buffer = put_playback_buffer;
	*loopback_output = iodev;
}

void loopback_iodev_destroy(struct cras_iodev *loopback_input,
			    struct cras_iodev *loopback_output)
{
	struct loopback_iodev *loopdev =
			(struct loopback_iodev *)loopback_output;
	struct shared_buffer *sbuf = loopdev->shared_buffer;

	cras_iodev_list_rm_input(loopback_input);

	free(sbuf->buffer);
	free(sbuf);
	free(loopback_input);
	free(loopback_output);
}

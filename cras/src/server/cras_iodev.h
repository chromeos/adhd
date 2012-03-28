/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * cras_iodev represents playback or capture devices on the system.  Each iodev
 * will spawn a thread it will use to render or capture audio.  For playback,
 * this thread will gather audio from the streams that are attached to the
 * device and render the samples it gets to the hardware device.  For capture
 * the process is reversed, the samples are pulled from hardware and passed on
 * to the attached streams.
 */
#ifndef CRAS_IODEV_H_
#define CRAS_IODEV_H_

#include "cras_iodev_info.h"
#include "cras_messages.h"

struct cras_rstream;
struct cras_audio_format;

/* Linked list of streams of audio from/to a client. */
struct cras_io_stream {
	struct cras_rstream *stream;
	int fd; /* cached here to to frequent access */
	struct cras_audio_shm_area *shm; /* ditto on caching */
	int mixed; /* Was this stream mixed already? */
	struct cras_io_stream *prev, *next;
};

/* An input or output device, that can have audio routed to/from it.
 * add_stream - Function to call when adding a stream.
 * rm_stream - Function to call when removing a stream.
 * format - The audio format being rendered or captured.
 * info - Unique identifier for this device (index and name).
 * streams - List of streams attached to device.
 * direction - Input or Output.
 * supported_rates - Array of sample rates supported by device 0-terminated.
 * supported_channel_counts - List of number of channels supported by device.
 */
struct cras_iodev {
	int (*add_stream)(struct cras_iodev *iodev,
			  struct cras_rstream *stream);
	int (*rm_stream)(struct cras_iodev *iodev,
			 struct cras_rstream *stream);
	struct cras_audio_format *format;
	struct cras_io_stream *streams;
	struct cras_iodev_info info;
	enum CRAS_STREAM_DIRECTION direction;
	size_t *supported_rates;
	size_t *supported_channel_counts;
	struct cras_iodev *prev, *next;
};

/* Define messages that can be sent from the main context to the device's
 * playback thread. */
enum CRAS_IODEV_COMMAND {
	CRAS_IODEV_ADD_STREAM,
	CRAS_IODEV_RM_STREAM,
	CRAS_IODEV_STOP,
};

struct cras_iodev_msg {
	size_t length;
	enum CRAS_IODEV_COMMAND id;
};

struct cras_iodev_add_rm_stream_msg {
	struct cras_iodev_msg header;
	struct cras_rstream *stream;
};

/* Utility functions to be used by iodev implementations. */
int cras_iodev_append_stream(struct cras_iodev *dev,
			     struct cras_rstream *stream);
int cras_iodev_delete_stream(struct cras_iodev *dev,
			     struct cras_rstream *stream);
/* Check if there are any streams playing on this device. */
static inline int cras_iodev_streams_attached(struct cras_iodev *dev)
{
	return dev->streams != NULL;
}

#endif /* CRAS_IODEV_H_ */

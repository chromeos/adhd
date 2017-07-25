/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <poll.h>
#include <syslog.h>

#include "audio_thread_log.h"
#include "cras_iodev.h"
#include "cras_rstream.h"
#include "cras_util.h"
#include "dev_stream.h"
#include "utlist.h"

#include "dev_io.h"

static const struct timespec playback_wake_fuzz_ts = {
	0, 500 * 1000 /* 500 usec. */
};

/* Reads any pending audio message from the socket. */
static void flush_old_aud_messages(struct cras_audio_shm *shm, int fd)
{
	struct audio_message msg;
	struct pollfd pollfd;
	int err;

	pollfd.fd = fd;
	pollfd.events = POLLIN;

	do {
		err = poll(&pollfd, 1, 0);
		if (pollfd.revents & POLLIN) {
			err = read(fd, &msg, sizeof(msg));
			cras_shm_set_callback_pending(shm, 0);
		}
	} while (err > 0);
}

/* Asks any stream with room for more data. Sets the time stamp for all streams.
 * Args:
 *    adev - The output device streams are attached to.
 * Returns:
 *    0 on success, negative error on failure. If failed, can assume that all
 *    streams have been removed from the device.
 */
static int fetch_streams(struct open_dev *adev)
{
	struct dev_stream *dev_stream;
	struct cras_iodev *odev = adev->dev;
	int rc;
	int delay;

	delay = cras_iodev_delay_frames(odev);
	if (delay < 0)
		return delay;

	DL_FOREACH(adev->dev->streams, dev_stream) {
		struct cras_rstream *rstream = dev_stream->stream;
		struct cras_audio_shm *shm =
			cras_rstream_output_shm(rstream);
		int fd = cras_rstream_get_audio_fd(rstream);
		const struct timespec *next_cb_ts;
		struct timespec now;

		clock_gettime(CLOCK_MONOTONIC_RAW, &now);

		if (cras_shm_callback_pending(shm) && fd >= 0) {
			flush_old_aud_messages(shm, fd);
			cras_rstream_record_fetch_interval(dev_stream->stream,
							   &now);
		}

		if (cras_shm_get_frames(shm) < 0)
			cras_rstream_set_is_draining(rstream, 1);

		if (cras_rstream_get_is_draining(dev_stream->stream))
			continue;

		next_cb_ts = dev_stream_next_cb_ts(dev_stream);
		if (!next_cb_ts)
			continue;

		/* Check if it's time to get more data from this stream.
		 * Allowing for waking up half a little early. */
		add_timespecs(&now, &playback_wake_fuzz_ts);
		if (!timespec_after(&now, next_cb_ts))
			continue;

		if (!dev_stream_can_fetch(dev_stream)) {
			ATLOG(atlog, AUDIO_THREAD_STREAM_SKIP_CB,
			      rstream->stream_id,
			      shm->area->write_offset[0],
			      shm->area->write_offset[1]);
			continue;
		}

		dev_stream_set_delay(dev_stream, delay);

		ATLOG(atlog, AUDIO_THREAD_FETCH_STREAM, rstream->stream_id,
		      cras_rstream_get_cb_threshold(rstream), delay);

		rc = dev_stream_request_playback_samples(dev_stream, &now);
		if (rc < 0) {
			syslog(LOG_ERR, "fetch err: %d for %x",
			       rc, rstream->stream_id);
			cras_rstream_set_is_draining(rstream, 1);
		}
	}

	return 0;
}

void dev_io_playback_fetch(struct open_dev *odev_list)
{
	struct open_dev *adev;

	DL_FOREACH(odev_list, adev) {
		if (!cras_iodev_is_open(adev->dev))
			continue;
		fetch_streams(adev);
	}
}

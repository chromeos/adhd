/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>
#include <syslog.h>

#include "cras_iodev.h"
#include "cras_rstream.h"
#include "utlist.h"

int cras_iodev_init(struct cras_iodev *dev,
		    enum CRAS_STREAM_DIRECTION direction,
		    int (*add_stream)(struct cras_iodev *iodev,
				      struct cras_rstream *stream),
		    int (*rm_stream)(struct cras_iodev *iodev,
				     struct cras_rstream *stream))
{
	int rc;

	dev->to_thread_fds[0] = -1;
	dev->to_thread_fds[1] = -1;
	dev->to_main_fds[0] = -1;
	dev->to_main_fds[1] = -1;
	dev->direction = direction;
	dev->rm_stream = rm_stream;
	dev->add_stream = add_stream;

	/* Two way pipes for communication with the device's audio thread. */
	rc = pipe(dev->to_thread_fds);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to pipe");
		return rc;
	}
	rc = pipe(dev->to_main_fds);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to pipe");
		return rc;
	}

	return 0;
}

void cras_iodev_deinit(struct cras_iodev *dev)
{
	if (dev->to_thread_fds[0] != -1) {
		close(dev->to_thread_fds[0]);
		close(dev->to_thread_fds[1]);
		dev->to_thread_fds[0] = -1;
		dev->to_thread_fds[1] = -1;
	}
	if (dev->to_main_fds[0] != -1) {
		close(dev->to_main_fds[0]);
		close(dev->to_main_fds[1]);
		dev->to_main_fds[0] = -1;
		dev->to_main_fds[1] = -1;
	}
}

int cras_iodev_append_stream(struct cras_iodev *dev,
			     struct cras_rstream *stream)
{
	struct cras_io_stream *out;

	/* Check that we don't already have this stream */
	DL_SEARCH_SCALAR(dev->streams, out, stream, stream);
	if (out != NULL)
		return -EEXIST;

	/* New stream, allocate a container and add it to the list. */
	out = calloc(1, sizeof(*out));
	if (out == NULL)
		return -ENOMEM;
	out->stream = stream;
	out->shm = cras_rstream_get_shm(stream);
	out->fd = cras_rstream_get_audio_fd(stream);
	DL_APPEND(dev->streams, out);

	return 0;
}

int cras_iodev_delete_stream(struct cras_iodev *dev,
			     struct cras_rstream *stream)
{
	struct cras_io_stream *out;

	/* Find stream, and if found, delete it. */
	DL_SEARCH_SCALAR(dev->streams, out, stream, stream);
	if (out == NULL)
		return -EINVAL;
	DL_DELETE(dev->streams, out);
	free(out);

	return 0;
}

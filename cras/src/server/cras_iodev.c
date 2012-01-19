/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_iodev.h"
#include "cras_rstream.h"
#include "utlist.h"

#include <stdlib.h>

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

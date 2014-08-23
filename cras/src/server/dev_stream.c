/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dev_stream.h"

struct dev_stream *dev_stream_create(struct cras_rstream *stream)
{
	struct dev_stream *out = calloc(1, sizeof(*out));
	out->stream = stream;

	return out;
}

void dev_stream_destroy(struct dev_stream *dev_stream)
{
	free(dev_stream);
}

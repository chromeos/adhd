/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The dev_stream structure is used for mapping streams to a device.  In
 * addition to the rstream, other mixing information is stored here.
 */

#ifndef DEV_STREAM_H_
#define DEV_STREAM_H_

#include <stdint.h>

#include "cras_types.h"

struct cras_rstream;

/*
 * Linked list of streams of audio from/to a client.
 * Args:
 *    stream - The rstream attached to a device.
 *    skip_mix - Don't mix this next time streams are mixed.
 */
struct dev_stream {
	struct cras_rstream *stream;
	unsigned int skip_mix; /* Skip this stream next mix cycle. */
	struct dev_stream *prev, *next;
};

struct dev_stream *dev_stream_create(struct cras_rstream *stream);
void dev_stream_destroy(struct dev_stream *dev_stream);

#endif /* DEV_STREAM_H_ */

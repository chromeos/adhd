/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>

#include "cras_types.h"
#include "buffer_share.h"

static inline struct dev_offset *find_dev(const struct buffer_share *mix,
					  unsigned int dev_id)
{
	unsigned int i;

	for (i = 0; i < mix->dev_sz; i++) {
		if (dev_id == mix->wr_idx[i].id)
			return &mix->wr_idx[i];
	}

	return NULL;
}

static void alloc_more_devs(struct buffer_share *mix)
{
	unsigned int new_size = mix->dev_sz * 2;
	unsigned int i;

	mix->wr_idx = realloc(mix->wr_idx, sizeof(mix->wr_idx[0]) * new_size);

	for (i = 0; i < mix->dev_sz; i++)
		mix->wr_idx[mix->dev_sz + i].id = NO_DEVICE;

	mix->dev_sz = new_size;
}

struct buffer_share *buffer_share_create(unsigned int buf_sz)
{
	struct buffer_share *mix;

	mix = calloc(1, sizeof(*mix));
	mix->dev_sz = INITIAL_DEV_SIZE;
	mix->wr_idx = calloc(mix->dev_sz, sizeof(mix->wr_idx[0]));
	mix->buf_sz = buf_sz;

	return mix;
}

void buffer_share_destroy(struct buffer_share *mix)
{
	if (!mix)
		return;
	free(mix->wr_idx);
	free(mix);
}

int buffer_share_add_dev(struct buffer_share *mix, unsigned int dev_id)
{
	struct dev_offset *o;

	o = find_dev(mix, dev_id);
	if (o)
		return -EEXIST;

	o = find_dev(mix, NO_DEVICE);
	if (!o)
		alloc_more_devs(mix);

	o = find_dev(mix, NO_DEVICE);
	o->id = dev_id;
	o->offset = mix->wr_point;

	return 0;
}

int buffer_share_rm_dev(struct buffer_share *mix, unsigned int dev_id)
{
	struct dev_offset *o;

	o = find_dev(mix, dev_id);
	if (!o)
		return -ENOENT;
	o->id = NO_DEVICE;

	return 0;
}

int buffer_share_offset_update(struct buffer_share *mix, unsigned int dev_id,
			       unsigned int delta)
{
	unsigned int i;

	for (i = 0; i < mix->dev_sz; i++) {
		if (dev_id != mix->wr_idx[i].id)
			continue;

		mix->wr_idx[i].offset += delta;
		mix->wr_idx[i].offset %= mix->buf_sz;

		break;
	}

	return 0;
}

unsigned int buffer_share_get_new_write_point(struct buffer_share *mix)
{
	unsigned int min_written = mix->buf_sz;
	unsigned int min_index = 0;
	unsigned int i;
	unsigned int distance;
	struct dev_offset *o;

	for (i = 0; i < mix->dev_sz; i++) {
		o = &mix->wr_idx[i];

		if (o->id == NO_DEVICE)
			continue;

		if (o->offset >= mix->wr_point)
			distance = o->offset - mix->wr_point;
		else
			distance = mix->buf_sz -
					(mix->wr_point - o->offset);

		if (distance < min_written) {
			min_written = distance;
			min_index = i;
		}
	}

	mix->wr_point = mix->wr_idx[min_index].offset;
	return min_written;
}

unsigned int buffer_share_dev_offset(const struct buffer_share *mix,
				     unsigned int dev_id)
{
	unsigned int i;
	struct dev_offset *o;

	for (i = 0; i < mix->dev_sz; i++) {
		o = &mix->wr_idx[i];

		if (o->id != dev_id)
			continue;

		if (o->offset >= mix->wr_point)
			return o->offset - mix->wr_point;
		else
			return mix->buf_sz - (mix->wr_point - o->offset);
	}

	return 0;
}

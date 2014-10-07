/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>
#include <sys/param.h>

#include "cras_types.h"
#include "buffer_share.h"

static inline struct dev_offset *find_unused(const struct buffer_share *mix)
{
	unsigned int i;

	for (i = 0; i < mix->dev_sz; i++) {
		if (!mix->wr_idx[i].used)
			return &mix->wr_idx[i];
	}

	return NULL;
}


static inline struct dev_offset *find_dev(const struct buffer_share *mix,
					  unsigned int dev_id)
{
	unsigned int i;

	for (i = 0; i < mix->dev_sz; i++) {
		if (mix->wr_idx[i].used && dev_id == mix->wr_idx[i].id)
			return &mix->wr_idx[i];
	}

	return NULL;
}

static void alloc_more_devs(struct buffer_share *mix)
{
	unsigned int new_size = mix->dev_sz * 2;
	unsigned int i;

	mix->wr_idx = realloc(mix->wr_idx, sizeof(mix->wr_idx[0]) * new_size);

	for (i = mix->dev_sz; i < new_size; i++)
		mix->wr_idx[i].used = 0;

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

	o = find_unused(mix);
	if (!o)
		alloc_more_devs(mix);

	o = find_unused(mix);
	o->used = 1;
	o->id = dev_id;
	o->offset = 0;

	return 0;
}

int buffer_share_rm_dev(struct buffer_share *mix, unsigned int dev_id)
{
	struct dev_offset *o;

	o = find_dev(mix, dev_id);
	if (!o)
		return -ENOENT;
	o->used = 0;

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
		break;
	}

	return 0;
}

unsigned int buffer_share_get_new_write_point(struct buffer_share *mix)
{
	unsigned int min_written = mix->buf_sz + 1;
	unsigned int i;

	for (i = 0; i < mix->dev_sz; i++) {
		struct dev_offset *o = &mix->wr_idx[i];

		if (!o->used)
			continue;

		min_written = MIN(min_written, o->offset);
	}
	for (i = 0; i < mix->dev_sz; i++) {
		struct dev_offset *o = &mix->wr_idx[i];
		o->offset -= min_written;
	}

	if (min_written > mix->buf_sz)
		return 0;

	return min_written;
}

unsigned int buffer_share_dev_offset(const struct buffer_share *mix,
				     unsigned int dev_id)
{
	unsigned int i;
	struct dev_offset *o;

	for (i = 0; i < mix->dev_sz; i++) {
		o = &mix->wr_idx[i];

		if (o->id == dev_id)
			return o->offset;
	}

	return 0;
}

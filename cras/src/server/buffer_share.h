/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef BUFFER_SHARE_H_
#define BUFFER_SHARE_H_

#define INITIAL_DEV_SIZE 3

struct dev_offset {
	unsigned int id;
	unsigned int offset;
};

struct buffer_share {
	unsigned int buf_sz;
	unsigned int dev_sz;
	unsigned int wr_point;
	struct dev_offset *wr_idx;
};

/*
 * Creates a buffer share object.  This object is used to manage the read or
 * write offsets of several devices in one shared buffer.
 */
struct buffer_share *buffer_share_create(unsigned int buf_sz);

/* Destroys a buffer_share returned from buffer_share_create. */
void buffer_share_destroy(struct buffer_share *mix);

/* Adds a device that shares the buffer. */
int buffer_share_add_dev(struct buffer_share *mix, unsigned int dev_id);

/* Removes a device that shares the buffer. */
int buffer_share_rm_dev(struct buffer_share *mix, unsigned int dev_id);

/* Updates the offset of the given device into the shared buffer. */
int buffer_share_offset_update(struct buffer_share *mix, unsigned int dev_id,
			       unsigned int frames);

/*
 * Updates the write point to the minimum offset from all devices.
 * Returns the number of minimum number of frames written.
 */
unsigned int buffer_share_get_new_write_point(struct buffer_share *mix);

/*
 * The amount by which the dev given by dev_id is ahead of the current write
 * point.
 */
unsigned int buffer_share_dev_offset(const struct buffer_share *mix,
				     unsigned int dev_id);

#endif /* BUFFER_SHARE_H_ */

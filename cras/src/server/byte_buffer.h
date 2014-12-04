/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

struct byte_buffer {
	unsigned int write_idx;
	unsigned int read_idx;
	unsigned int level;
	unsigned int size;
	uint8_t bytes[];
};

/* Create a byte buffer to hold buffer_size_bytes worth of data. */
static inline struct byte_buffer *byte_buffer_create(size_t buffer_size_bytes)
{
	struct byte_buffer *buf;
	buf = (struct byte_buffer *)
		calloc(1, sizeof(struct byte_buffer) + buffer_size_bytes);
	if (!buf)
		return buf;
	buf->size = buffer_size_bytes;
	return buf;
}

/* Destory a byte_buffer created with byte_buffer_create. */
static inline void byte_buffer_destroy(struct byte_buffer *buf)
{
	free(buf);
}

static inline unsigned int buf_writable_bytes(struct byte_buffer *buf)
{
	if (buf->level >= buf->size)
		return 0;
	if (buf->write_idx < buf->read_idx)
		return buf->read_idx - buf->write_idx;

	return buf->size - buf->write_idx;
}

static inline unsigned int buf_readable_bytes(struct byte_buffer *buf)
{
	if (buf->level == 0)
		return 0;

	if (buf->read_idx < buf->write_idx)
		return buf->write_idx - buf->read_idx;

	return buf->size - buf->read_idx;
}

static inline unsigned int buf_queued_bytes(struct byte_buffer *buf)
{
	return buf->level;
}

static inline unsigned int buf_available_bytes(const struct byte_buffer *buf)
{
	return buf->size - buf->level;
}

static inline uint8_t *buf_read_pointer(struct byte_buffer *buf)
{
	return &buf->bytes[buf->read_idx];
}

static inline uint8_t *buf_read_pointer_size(struct byte_buffer *buf,
					     unsigned int *readable)
{
	*readable = buf_readable_bytes(buf);
	return buf_read_pointer(buf);
}

static inline void buf_increment_read(struct byte_buffer *buf, size_t inc)
{
	inc = MIN(inc, buf->level);
	buf->read_idx += inc;
	buf->read_idx %= buf->size;
	buf->level -= inc;
}

static inline uint8_t *buf_write_pointer(struct byte_buffer *buf)
{
	return &buf->bytes[buf->write_idx];
}

static inline uint8_t *buf_write_pointer_size(struct byte_buffer *buf,
					      unsigned int *writeable)
{
	*writeable = buf_writable_bytes(buf);
	return buf_write_pointer(buf);
}

static inline void buf_increment_write(struct byte_buffer *buf, size_t inc)
{
	buf->write_idx += inc;
	buf->write_idx %= buf->size;
	if (buf->level + inc < buf->size)
		buf->level += inc;
	else
		buf->level = buf->size;
}

static inline void buf_reset(struct byte_buffer *buf)
{
	buf->write_idx = 0;
	buf->read_idx = 0;
	buf->level = 0;
}

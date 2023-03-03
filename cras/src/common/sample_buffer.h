/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Sample buffer helps manipulate buffer in the granularity of samples.
 *
 * Usage:
 *   1. sample_buffer_init and sample_buffer_cleanup:
 *     In this way, sample_buffer will own the underlying byte_buffer and is
 *     responsible for freeing it. We'd better not touching the underlying
 *     buffer directly. Instead, we should only use the provided functions to
 *     interact with the sample_buffer.
 *   2. sample_buffer_weak_ref:
 *     In this way, sample_buffer creates a weak reference of a byte_buffer,
 *     and won't free it.
 */
#ifndef CRAS_SRC_COMMON_SAMPLE_BUFFER_H_
#define CRAS_SRC_COMMON_SAMPLE_BUFFER_H_

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "cras/src/common/byte_buffer.h"

// A sample buffer.
struct sample_buffer {
  // the number of bytes of each sample in the buffer.
  size_t sample_size;
  // the byte_buffer that stores the data.
  struct byte_buffer* buf;
};

/* Inits a sample_buffer.
 *
 * Example usage:
 *    struct sample_buffer buf = {};
 *    int rc = sample_buffer_init(&buf, 100, 4);
 *    if (rc != 0)
 *      <error handling>;
 *    <interaction with sample_buf>
 *    sample_buffer_cleanup(&buf);
 *
 * Args:
 *    num_samples - number of samples in the sample_buffer.
 *    sample_size - size of each sample in the sample_buffer.
 *    buf - the sample_buffer to be initialized.
 * Returns:
 *    0 on success, otherwise a negative error code.
 */
static inline int sample_buffer_init(const size_t num_samples,
                                     const size_t sample_size,
                                     struct sample_buffer* buf) {
  struct byte_buffer* internal_buf =
      byte_buffer_create(num_samples * sample_size);
  if (!internal_buf) {
    return -ENOMEM;
  }

  struct sample_buffer temp_buf = {.sample_size = sample_size,
                                   .buf = internal_buf};
  memcpy(buf, &temp_buf, sizeof(struct sample_buffer));

  return 0;
}

/* Cleans up a sample_buffer.
 *
 * Args:
 *    buf - the sample_buffer to be cleaned up.
 */
static inline void sample_buffer_cleanup(struct sample_buffer* buf) {
  if (buf && buf->buf) {
    byte_buffer_destroy(&buf->buf);
  }
}

/* Checks if the sample_buf works well with the given byte_buffer.
 *
 * Args:
 *    buf - the byte_buffer that is going to be validated.
 *    sample_size - size of each sample that used to validate the byte_buffer.
 * Returns:
 *    1 on success, otherwise 0.
 */
static inline int sample_buffer_validate_byte_buffer(struct byte_buffer* buf,
                                                     const size_t sample_size) {
  return buf && (sample_size != 0) && (buf->used_size % sample_size == 0) &&
         (buf->read_idx % sample_size == 0) &&
         (buf->write_idx % sample_size == 0) && (buf->level % sample_size == 0);
}

/* Creates a sample_buffer that uses the given buf as its internal buffer.
 *
 * Do not call sample_buffer_cleanup for buffers created by this function.
 *
 * Args:
 *    ref_buf - the byte_buffer to be referenced weakly.
 *    sample_size - size of each sample.
 * Returns:
 *    A sample_buffer that uses the given ref_buf as its internal buffer.
 */
static inline struct sample_buffer sample_buffer_weak_ref(
    struct byte_buffer* ref_buf,
    const size_t sample_size) {
  assert(sample_buffer_validate_byte_buffer(ref_buf, sample_size) &&
         "sample_buffer_validate_byte_buffer failed.");
  struct sample_buffer buf = {.sample_size = sample_size, .buf = ref_buf};
  return buf;
}

/* Gets the number of readable samples.
 *
 * Args:
 *    buf - the sample_buffer.
 * Returns:
 *    A unsigned integer indicating the number of readable samples.
 */
static inline unsigned int sample_buf_readable(
    const struct sample_buffer* buf) {
  return buf_readable(buf->buf) / buf->sample_size;
}

/* Gets the number of queued samples.
 *
 * Args:
 *    buf - the sample_buffer.
 * Returns:
 *    A unsigned integer indicating the number of queued samples.
 */
static inline unsigned int sample_buf_queued(const struct sample_buffer* buf) {
  return buf_queued(buf->buf) / buf->sample_size;
}

/* Gets the pointer for reading samples.
 *
 * Args:
 *    buf - the sample_buffer.
 * Returns:
 *    A pointer pointing to the head sample to read.
 */
static inline uint8_t* sample_buf_read_pointer(struct sample_buffer* buf) {
  return buf_read_pointer(buf->buf);
}

/* Gets the pointer for reading samples and the number of readable samples.
 *
 * Args:
 *    buf - the sample_buffer.
 *    num_readable_samples - the number of readable samples.
 * Returns:
 *    A pointer pointing to the head sample to read.
 */
static inline uint8_t* sample_buf_read_pointer_size(
    struct sample_buffer* buf,
    unsigned int* num_readable_samples) {
  *num_readable_samples = sample_buf_readable(buf);
  return sample_buf_read_pointer(buf);
}

/* Increments the internal read pointer by num_inc_samples.
 *
 * Args:
 *    buf - the sample_buffer.
 *    num_inc_samples - the number of samples for the internal read pointer to
 *      increment.
 */
static inline void sample_buf_increment_read(struct sample_buffer* buf,
                                             size_t num_inc_samples) {
  buf_increment_read(buf->buf, num_inc_samples * buf->sample_size);
}

/* Gets the number of writable space in sample.
 *
 * Args:
 *    buf - the sample_buffer.
 * Returns:
 *    A unsigned integer indicating the number of writable space in sample.
 */
static inline unsigned int sample_buf_writable(
    const struct sample_buffer* buf) {
  return buf_writable(buf->buf) / buf->sample_size;
}

/* Gets the number of available space in sample.
 *
 * Args:
 *    buf - the sample_buffer.
 * Returns:
 *    A unsigned integer indicating the number of available space in sample.
 */
static inline unsigned int sample_buf_available(
    const struct sample_buffer* buf) {
  return buf_available(buf->buf) / buf->sample_size;
}

/* Gets the pointer for writing samples.
 *
 * Args:
 *    buf - the sample_buffer.
 * Returns:
 *    A pointer pointing to the head position to write.
 */
static inline uint8_t* sample_buf_write_pointer(struct sample_buffer* buf) {
  return buf_write_pointer(buf->buf);
}

/* Gets the pointer for writing samples and the number of writable space in
 * sample.
 *
 * Args:
 *    buf - the sample_buffer.
 *    num_writable_samples - the number of writable space in sample.
 * Returns:
 *    A pointer pointing to the head position to write.
 */
static inline uint8_t* sample_buf_write_pointer_size(
    struct sample_buffer* buf,
    unsigned int* num_writable_samples) {
  *num_writable_samples = sample_buf_writable(buf);
  return sample_buf_write_pointer(buf);
}

/* Increments the internal write pointer by num_inc_samples.
 *
 * Args:
 *    buf - the sample_buffer.
 *    num_inc_samples - the number of samples for the internal write pointer
 *      to increment.
 */
static inline void sample_buf_increment_write(struct sample_buffer* buf,
                                              size_t num_inc_samples) {
  buf_increment_write(buf->buf, num_inc_samples * buf->sample_size);
}

/* Resets the sample_buffer.
 *
 * Args:
 *    buf - the sample_buffer.
 */
static inline void sample_buf_reset(struct sample_buffer* buf) {
  buf_reset(buf->buf);
}

/* Indicates if the buffer is full and the internal read index is zero.
 *
 * Args:
 *    buf - the sample_buffer.
 * Returns:
 *    1 if the buffer is full and the internal read index is zero, otherwise 0.
 */
static inline int sample_buf_full_with_zero_read_index(
    struct sample_buffer* buf) {
  return sample_buf_readable(buf) == buf->buf->used_size / buf->sample_size;
}

// Returns the size of each sample in the buffer.
static inline size_t sample_buf_get_sample_size(
    const struct sample_buffer* buf) {
  return buf->sample_size;
}

/* Returns the underlying byte_buffer.
 * It's useful for interacting with functions who take byte_buffers as inputs.
 * However, this function must be used carefully since the sample buffer may
 * become invalid due to being inconsistent with the byte buffer.
 * Use `sample_buffer_validate_byte_buffer` to check the state.
 */
static inline struct byte_buffer* sample_buf_get_buf(
    struct sample_buffer* buf) {
  return buf->buf;
}

#endif  // CRAS_SRC_COMMON_SAMPLE_BUFFER_H_

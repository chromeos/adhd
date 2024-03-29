/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/buffer_share.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/param.h>

static inline struct id_offset* find_unused(const struct buffer_share* mix) {
  unsigned int i;

  for (i = 0; i < mix->id_sz; i++) {
    if (!mix->wr_idx[i].used) {
      return &mix->wr_idx[i];
    }
  }

  return NULL;
}

static inline struct id_offset* find_id(const struct buffer_share* mix,
                                        unsigned int id) {
  unsigned int i;

  for (i = 0; i < mix->id_sz; i++) {
    if (mix->wr_idx[i].used && id == mix->wr_idx[i].id) {
      return &mix->wr_idx[i];
    }
  }

  return NULL;
}

static void alloc_more_ids(struct buffer_share* mix) {
  unsigned int new_size = mix->id_sz * 2;
  unsigned int i;

  mix->wr_idx = (struct id_offset*)realloc(mix->wr_idx,
                                           sizeof(mix->wr_idx[0]) * new_size);

  for (i = mix->id_sz; i < new_size; i++) {
    mix->wr_idx[i].used = 0;
  }

  mix->id_sz = new_size;
}

struct buffer_share* buffer_share_create(unsigned int buf_sz) {
  struct buffer_share* mix;

  mix = (struct buffer_share*)calloc(1, sizeof(*mix));
  mix->id_sz = INITIAL_ID_SIZE;
  mix->wr_idx = (struct id_offset*)calloc(mix->id_sz, sizeof(mix->wr_idx[0]));
  mix->buf_sz = buf_sz;

  return mix;
}

void buffer_share_destroy(struct buffer_share* mix) {
  if (!mix) {
    return;
  }
  free(mix->wr_idx);
  free(mix);
}

int buffer_share_add_id(struct buffer_share* mix, unsigned int id, void* data) {
  struct id_offset* o;

  o = find_id(mix, id);
  if (o) {
    return -EEXIST;
  }

  o = find_unused(mix);
  if (!o) {
    alloc_more_ids(mix);
  }

  o = find_unused(mix);
  o->used = 1;
  o->id = id;
  o->offset = 0;
  o->data = data;

  return 0;
}

int buffer_share_rm_id(struct buffer_share* mix, unsigned int id) {
  struct id_offset* o;

  o = find_id(mix, id);
  if (!o) {
    return -ENOENT;
  }
  o->used = 0;
  o->data = NULL;

  return 0;
}

int buffer_share_offset_update(struct buffer_share* mix,
                               unsigned int id,
                               unsigned int delta) {
  unsigned int i;

  for (i = 0; i < mix->id_sz; i++) {
    if (id != mix->wr_idx[i].id) {
      continue;
    }

    mix->wr_idx[i].offset += delta;
    break;
  }

  return 0;
}

unsigned int buffer_share_get_minimum_offset(struct buffer_share* mix) {
  unsigned int min_written = mix->buf_sz + 1;

  for (unsigned int i = 0; i < mix->id_sz; i++) {
    struct id_offset* o = &mix->wr_idx[i];

    if (!o->used) {
      continue;
    }

    min_written = MIN(min_written, o->offset);
  }
  if (min_written > mix->buf_sz) {
    return 0;
  }
  return min_written;
}

int buffer_share_update_write_point(struct buffer_share* mix,
                                    unsigned int written) {
  for (unsigned int i = 0; i < mix->id_sz; i++) {
    struct id_offset* o = &mix->wr_idx[i];

    if (!o->used) {
      continue;
    }

    if (o->offset < written) {
      return -EINVAL;
    }
  }
  for (unsigned int i = 0; i < mix->id_sz; i++) {
    struct id_offset* o = &mix->wr_idx[i];
    o->offset -= written;
  }

  return 0;
}

unsigned int buffer_share_get_new_write_point(struct buffer_share* mix) {
  unsigned int minimum_offset = buffer_share_get_minimum_offset(mix);
  int rc = buffer_share_update_write_point(mix, minimum_offset);
  if (rc < 0) {
    return 0;
  }
  return minimum_offset;
}

static struct id_offset* get_id_offset(const struct buffer_share* mix,
                                       unsigned int id) {
  unsigned int i;
  struct id_offset* o;

  for (i = 0; i < mix->id_sz; i++) {
    o = &mix->wr_idx[i];
    if (o->used && o->id == id) {
      return o;
    }
  }
  return NULL;
}

unsigned int buffer_share_id_offset(const struct buffer_share* mix,
                                    unsigned int id) {
  struct id_offset* o = get_id_offset(mix, id);
  return o ? o->offset : 0;
}

void* buffer_share_get_data(const struct buffer_share* mix, unsigned int id) {
  struct id_offset* o = get_id_offset(mix, id);
  return o ? o->data : NULL;
}

int buffer_share_reset_write_point(struct buffer_share* mix) {
  // The paths that call this function may end up having removed all streams.
  // Add a null check to prevent crash.
  if (mix != NULL) {
    for (unsigned int i = 0; i < mix->id_sz; i++) {
      struct id_offset* o = &mix->wr_idx[i];
      if (!o->used) {
        continue;
      }
      o->offset = 0;
    }
  }
  return 0;
}

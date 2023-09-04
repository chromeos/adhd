/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cras/src/common/blob_wrapper.h"

#define TLV_BUFFER_TYPE_OFFSET 0
#define TLV_BUFFER_LENGTH_OFFSET 1
#define TLV_BUFFER_VALUE_OFFSET 2

#define TLV_BUFFER_HEADER_SIZE (2 * sizeof(uint32_t))

struct tlv_blob_wrapper {
  struct blob_wrapper base;
  // TODO(johnylin): type is undetermined as for now. Revisit this on demand.
  uint32_t type;
};

static int tlv_blob_get_wrapped_size(struct blob_wrapper* bw,
                                     const uint8_t* src,
                                     size_t src_size) {
  return src_size + TLV_BUFFER_HEADER_SIZE;
}

static int tlv_blob_wrap(struct blob_wrapper* bw,
                         uint8_t* dst,
                         const uint8_t* src,
                         size_t src_size) {
  struct tlv_blob_wrapper* tlv_bw = (struct tlv_blob_wrapper*)bw;
  uint32_t* dst_u32;

  if (!src) {
    // Invalid blob source.
    return -EINVAL;
  }

  dst_u32 = (uint32_t*)dst;

  // Fill the TLV type and length information.
  dst_u32[TLV_BUFFER_TYPE_OFFSET] = tlv_bw->type;
  dst_u32[TLV_BUFFER_LENGTH_OFFSET] = src_size;

  // Wrap the source blob.
  memcpy(&dst_u32[TLV_BUFFER_VALUE_OFFSET], src, src_size);
  return 0;
}

static int tlv_blob_get_unwrapped_size(struct blob_wrapper* bw,
                                       const uint8_t* src,
                                       size_t src_size) {
  uint32_t* src_u32;

  if (!src || src_size < TLV_BUFFER_HEADER_SIZE) {
    return -EINVAL;
  }

  src_u32 = (uint32_t*)src;
  return src_u32[TLV_BUFFER_LENGTH_OFFSET];
}

static int tlv_blob_unwrap(struct blob_wrapper* bw,
                           uint8_t* dst,
                           const uint8_t* src,
                           size_t unwrapped_size) {
  uint32_t* src_u32;

  if (!src) {
    // Invalid blob source.
    return -EINVAL;
  }

  src_u32 = (uint32_t*)src;
  memcpy(dst, &src_u32[TLV_BUFFER_VALUE_OFFSET], unwrapped_size);
  return 0;
}

static const struct blob_wrapper_ops tlv_blob_wrapper_ops = {
    .get_wrapped_size = tlv_blob_get_wrapped_size,
    .wrap = tlv_blob_wrap,
    .get_unwrapped_size = tlv_blob_get_unwrapped_size,
    .unwrap = tlv_blob_unwrap,
};

struct blob_wrapper* tlv_blob_wrapper_create() {
  struct tlv_blob_wrapper* tlv_bw;
  struct blob_wrapper* base;

  tlv_bw = (struct tlv_blob_wrapper*)calloc(1, sizeof(*tlv_bw));
  if (!tlv_bw) {
    return NULL;
  }

  base = &tlv_bw->base;
  base->ops = &tlv_blob_wrapper_ops;
  return base;
}

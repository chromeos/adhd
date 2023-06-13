/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/common/blob_wrapper.h"

int blob_wrapper_get_wrapped_size(struct blob_wrapper* bw,
                                  const uint8_t* src,
                                  size_t src_size) {
  if (!bw || !bw->ops->get_wrapped_size) {
    return -EINVAL;
  }

  return bw->ops->get_wrapped_size(bw, src, src_size);
}

int blob_wrapper_wrap(struct blob_wrapper* bw,
                      uint8_t* dst,
                      size_t dst_size,
                      const uint8_t* src,
                      size_t src_size) {
  int wrapped_size;
  int rc;

  if (!bw || !bw->ops->wrap) {
    return -EINVAL;
  }

  if (!dst) {
    // The blob buffer needs to be allocated by the caller.
    return -EINVAL;
  }

  rc = blob_wrapper_get_wrapped_size(bw, src, src_size);
  if (rc < 0) {
    return rc;
  }
  if (dst_size < rc) {
    // The blob buffer size is insufficient.
    return -E2BIG;
  }
  wrapped_size = rc;

  rc = bw->ops->wrap(bw, dst, src, src_size);
  if (rc < 0) {
    return rc;
  }
  return wrapped_size;
}

int blob_wrapper_get_unwrapped_size(struct blob_wrapper* bw,
                                    const uint8_t* src,
                                    size_t src_size) {
  if (!bw || !bw->ops->get_unwrapped_size) {
    return -EINVAL;
  }

  return bw->ops->get_unwrapped_size(bw, src, src_size);
}

int blob_wrapper_unwrap(struct blob_wrapper* bw,
                        uint8_t* dst,
                        size_t dst_size,
                        const uint8_t* src,
                        size_t src_size) {
  size_t unwrapped_size;
  int rc;

  if (!bw || !bw->ops->unwrap) {
    return -EINVAL;
  }

  if (!dst) {
    // The blob buffer needs to be allocated by the caller.
    return -EINVAL;
  }

  rc = blob_wrapper_get_unwrapped_size(bw, src, src_size);
  if (rc < 0) {
    return rc;
  }
  if (dst_size < rc) {
    // The blob buffer size is insufficient.
    return -E2BIG;
  }
  unwrapped_size = rc;

  rc = bw->ops->unwrap(bw, dst, src, unwrapped_size);
  if (rc < 0) {
    return rc;
  }
  return unwrapped_size;
}

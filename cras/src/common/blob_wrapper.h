/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_BLOB_WRAPPER_H_
#define CRAS_SRC_COMMON_BLOB_WRAPPER_H_

#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct blob_wrapper;

/* blob_wrapper_ops is the interface for bytes data formatting.
 *
 * To keep clean, blob_wrapper_ops instances shouldn't allocate dynamic memory.
 * Instead, the client should call get_(un)wrapped_size() and then allocate
 * memory for the blob placeholder of the desired size.
 */
struct blob_wrapper_ops {
  int (*get_wrapped_size)(struct blob_wrapper* bw,
                          const uint8_t* src,
                          size_t src_size);
  int (*wrap)(struct blob_wrapper* bw,
              uint8_t* dst,
              const uint8_t* src,
              size_t src_size);
  int (*get_unwrapped_size)(struct blob_wrapper* bw,
                            const uint8_t* src,
                            size_t src_size);
  int (*unwrap)(struct blob_wrapper* bw,
                uint8_t* dst,
                const uint8_t* src,
                size_t unwrapped_size);
};

// The base of blob wrapper instances.
struct blob_wrapper {
  const struct blob_wrapper_ops* ops;
};

/*
 * Factory API functions
 */

// Creates a TLV blob wrapper.
struct blob_wrapper* tlv_blob_wrapper_create();
// Creates a blob wrapper for SOF configuration.
struct blob_wrapper* sof_blob_wrapper_create();

/*
 * Base-layer API functions
 */

/* Gets the size after wrapping from the blob in src.
 * Args:
 *    bw - The blob wrapper.
 *    src - The pointer of the source blob to wrap.
 *    src_size - The source blob size in bytes.
 * Returns:
 *    The unwrapped size in bytes, otherwise a negative error code.
 */
int blob_wrapper_get_wrapped_size(struct blob_wrapper* bw,
                                  const uint8_t* src,
                                  size_t src_size);

/* Wraps the blob in src and writes to dst.
 * Args:
 *    bw - The blob wrapper.
 *    dst - The allocated buffer pointer for the wrapped blob placeholder.
 *    dst_size - The blob placeholder size in bytes.
 *    src - The pointer of the source blob to wrap.
 *    src_size - The source blob size in bytes.
 * Returns:
 *    The wrapped blob size in bytes, otherwise a negative error code.
 */
int blob_wrapper_wrap(struct blob_wrapper* bw,
                      uint8_t* dst,
                      size_t dst_size,
                      const uint8_t* src,
                      size_t src_size);

/* Gets the size after unwrapping from the blob in src.
 * Args:
 *    bw - The blob wrapper.
 *    src - The pointer of the source blob to unwrap.
 *    src_size - The source blob size in bytes.
 * Returns:
 *    The unwrapped size in bytes, otherwise a negative error code.
 */
int blob_wrapper_get_unwrapped_size(struct blob_wrapper* bw,
                                    const uint8_t* src,
                                    size_t src_size);

/* Unwraps the blob in src and writes to dst.
 * Args:
 *    bw - The blob wrapper.
 *    dst - The allocated buffer pointer for the unwrapped blob placeholder.
 *    dst_size - The blob placeholder size in bytes.
 *    src - The pointer of the source blob to unwrap.
 *    src_size - The source blob size in bytes.
 * Returns:
 *    The unwrapped blob size in bytes, otherwise a negative error code.
 */
int blob_wrapper_unwrap(struct blob_wrapper* bw,
                        uint8_t* dst,
                        size_t dst_size,
                        const uint8_t* src,
                        size_t src_size);

#ifdef __cplusplus
}
#endif

#endif  // CRAS_SRC_COMMON_BLOB_WRAPPER_H_

/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <sound/sof/abi.h>
#include <sound/sof/header.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cras/src/common/blob_wrapper.h"

/* These definitions are aligned to "enum sof_ipc_ctrl_cmd" under SOF kernel
 * include/sound/sof/control.h
 */
#define SOF_CTRL_CMD_BINARY 3

/* These definitions are aligned to
 * https://github.com/thesofproject/sof/blob/main/tools/ctl/ctl.c#L19
 */
#define SOF_CTRL_BUFFER_TAG_OFFSET 0
#define SOF_CTRL_BUFFER_SIZE_OFFSET 1
#define SOF_CTRL_BUFFER_ABI_OFFSET 2

#define SOF_CTRL_BUFFER_DATA_OFFSET \
  (SOF_CTRL_BUFFER_ABI_OFFSET + sizeof(struct sof_abi_hdr) / sizeof(uint32_t))

#define SOF_CTRL_BUFFER_HEADER_SIZE \
  (sizeof(struct sof_abi_hdr) + 2 * sizeof(uint32_t))

/* sof_blob_wrapper provides bytes data manipulation for SOF-backed DSP
 * byte-typed configuration controls.The wrapping/unwrapping is as depicted
 * below.
 *
 * byte# 0   4   8                40                                       40+N
 *       [T ][S ][ABI_HEADER     ][CONFIG_BLOB                     .......]
 *     TAG^   ^SIZE
 *                                |<------------ unwrapped blob --------->|
 *                                            |             ^
 *       |<----- envelope ------>|            | wrap()      | unwrap()
 *                                            v             |
 *       |<--------------------- wrapped blob --------------------------->|
 *
 * The envelope consists of parameters:
 *    TAG - assigned SOF_CTRL_CMD_BINARY.
 *    SIZE - The total byte length for the blob excluding TAG and SIZE.
 *    ABI_HEADER - The fixed-length SOF-specific header.
 *
 * In use of SOF configuring, CRAS is lack of the information of ABI header for
 * the present DSP. Therefore, the preliminary configuration read is required
 * prior to any request for configuration write with blob wrapping. During the
 * preliminary read, the DSP-side blob will be received then extracted the ABI
 * header information on blob unwrapping.
 */
struct sof_blob_wrapper {
  struct blob_wrapper base;
  struct sof_abi_hdr abi_header;
};

static int sof_blob_get_wrapped_size(struct blob_wrapper* bw,
                                     const uint8_t* src,
                                     size_t src_size) {
  return src_size + SOF_CTRL_BUFFER_HEADER_SIZE;
}

static int sof_blob_wrap(struct blob_wrapper* bw,
                         uint8_t* dst,
                         const uint8_t* src,
                         size_t src_size) {
  struct sof_blob_wrapper* sof_bw = (struct sof_blob_wrapper*)bw;
  uint32_t* dst_u32;
  int config_size;
  struct sof_abi_hdr* hdr;

  if (!src) {
    // Invalid source blob.
    return -EINVAL;
  }

  config_size = src_size + sizeof(*hdr);
  dst_u32 = (uint32_t*)dst;

  // Fill the TAG and SIZE information.
  dst_u32[SOF_CTRL_BUFFER_TAG_OFFSET] = SOF_CTRL_CMD_BINARY;
  dst_u32[SOF_CTRL_BUFFER_SIZE_OFFSET] = config_size;

  // Fill sof_abi_hdr.
  hdr = (struct sof_abi_hdr*)&dst_u32[SOF_CTRL_BUFFER_ABI_OFFSET];
  memcpy(hdr, &sof_bw->abi_header, sizeof(*hdr));
  hdr->size = src_size;

  // Wrap the source blob.
  memcpy(&dst_u32[SOF_CTRL_BUFFER_DATA_OFFSET], src, src_size);
  return 0;
}

static int sof_blob_get_unwrapped_size(struct blob_wrapper* bw,
                                       const uint8_t* src,
                                       size_t src_size) {
  int unwrapped_size;
  uint32_t* src_u32;

  if (!src || src_size < SOF_CTRL_BUFFER_HEADER_SIZE) {
    // Invalid source blob.
    return -EINVAL;
  }

  src_u32 = (uint32_t*)src;
  unwrapped_size = src_u32[SOF_CTRL_BUFFER_SIZE_OFFSET];
  unwrapped_size -= sizeof(struct sof_abi_hdr);
  if (unwrapped_size < 0) {
    return -EINVAL;
  }
  return unwrapped_size;
}

static void sof_blob_update_abi_hdr(struct blob_wrapper* bw,
                                    const uint8_t* src) {
  struct sof_blob_wrapper* sof_bw = (struct sof_blob_wrapper*)bw;
  uint32_t* src_u32 = (uint32_t*)src;
  struct sof_abi_hdr* hdr =
      (struct sof_abi_hdr*)&src_u32[SOF_CTRL_BUFFER_ABI_OFFSET];

  sof_bw->abi_header.magic = hdr->magic;
  sof_bw->abi_header.type = hdr->type;
  sof_bw->abi_header.abi = hdr->abi;
}

static int sof_blob_unwrap(struct blob_wrapper* bw,
                           uint8_t* dst,
                           const uint8_t* src,
                           size_t unwrapped_size) {
  uint32_t* src_u32;

  if (!src) {
    // Invalid source blob.
    return -EINVAL;
  }

  // Update the content of sof_abi_hdr per unwrap calling.
  sof_blob_update_abi_hdr(bw, src);

  src_u32 = (uint32_t*)src;
  memcpy(dst, &src_u32[SOF_CTRL_BUFFER_DATA_OFFSET], unwrapped_size);
  return 0;
}

static const struct blob_wrapper_ops sof_blob_wrapper_ops = {
    .get_wrapped_size = sof_blob_get_wrapped_size,
    .wrap = sof_blob_wrap,
    .get_unwrapped_size = sof_blob_get_unwrapped_size,
    .unwrap = sof_blob_unwrap,
};

struct blob_wrapper* sof_blob_wrapper_create() {
  struct sof_blob_wrapper* sof_bw;
  struct blob_wrapper* base;

  sof_bw = (struct sof_blob_wrapper*)calloc(1, sizeof(*sof_bw));
  if (!sof_bw) {
    return NULL;
  }

  base = &sof_bw->base;
  base->ops = &sof_blob_wrapper_ops;

  sof_bw->abi_header.magic = SOF_ABI_MAGIC;
  sof_bw->abi_header.type = 0;
  sof_bw->abi_header.abi = SOF_ABI_VERSION;

  return base;
}

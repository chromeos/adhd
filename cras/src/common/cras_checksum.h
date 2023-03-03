/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_CHECKSUM_H_
#define CRAS_SRC_COMMON_CRAS_CHECKSUM_H_

#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t crc32_checksum(const unsigned char* input, size_t n);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_COMMON_CRAS_CHECKSUM_H_

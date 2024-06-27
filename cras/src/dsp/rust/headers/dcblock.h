// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SRC_DSP_RUST_HEADERS_DCBLOCK_H_
#define CRAS_SRC_DSP_RUST_HEADERS_DCBLOCK_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct dcblock;

struct dcblock *dcblock_new(void);

void dcblock_free(struct dcblock *dcblock);

void dcblock_set_config(struct dcblock *dcblock, float r, unsigned long sample_rate);

void dcblock_process(struct dcblock *dcblock, float *data, int32_t count);

struct dcblock *dcblock_new(void);

void dcblock_free(struct dcblock *dcblock);

void dcblock_set_config(struct dcblock *dcblock, float r, unsigned long sample_rate);

void dcblock_process(struct dcblock *dcblock, float *data, int32_t count);

#endif /* CRAS_SRC_DSP_RUST_HEADERS_DCBLOCK_H_ */

#ifdef __cplusplus
}
#endif

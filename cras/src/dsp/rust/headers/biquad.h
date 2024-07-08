// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SRC_DSP_RUST_HEADERS_BIQUAD_H_
#define CRAS_SRC_DSP_RUST_HEADERS_BIQUAD_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

enum biquad_type {
  BQ_NONE,
  BQ_LOWPASS,
  BQ_HIGHPASS,
  BQ_BANDPASS,
  BQ_LOWSHELF,
  BQ_HIGHSHELF,
  BQ_PEAKING,
  BQ_NOTCH,
  BQ_ALLPASS,
};

struct biquad {
  float b0;
  float b1;
  float b2;
  float a1;
  float a2;
  float x1;
  float x2;
  float y1;
  float y2;
};

struct biquad biquad_new_set(enum biquad_type enum_type, double freq, double q, double gain);

struct biquad biquad_new_set(enum biquad_type enum_type, double freq, double q, double gain);

#endif /* CRAS_SRC_DSP_RUST_HEADERS_BIQUAD_H_ */

#ifdef __cplusplus
}
#endif

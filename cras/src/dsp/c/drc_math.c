// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/dsp/c/drc_math.h"

#include <math.h>

float db_to_linear[201];  // from -100dB to 100dB

void drc_math_init() {
  int i;
  for (i = -100; i <= 100; i++) {
    db_to_linear[i + 100] = pow(10, i / 20.0);
  }
}

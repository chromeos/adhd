// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "cras/common/rust_common.h"

void print_cras_stream_active_ap_effects(FILE* f,
                                         CRAS_STREAM_ACTIVE_AP_EFFECT effects) {
  char* s = cras_stream_active_ap_effects_string(effects);
  fprintf(f, "%s", s);
  cras_rust_free_string(s);
}

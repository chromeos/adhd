// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/common/cras_types_internal.h"

#include <stdio.h>

void print_cras_stream_active_effects(FILE* f,
                                      enum CRAS_STREAM_ACTIVE_EFFECT effects) {
  if (!effects) {
    fprintf(f, " none");
    return;
  }
  if (effects & AE_ECHO_CANCELLATION) {
    fprintf(f, " echo_cancellation");
  }
  if (effects & AE_NOISE_SUPPRESSION) {
    fprintf(f, " noise_suppression");
  }
  if (effects & AE_VOICE_ACTIVITY_DETECTION) {
    fprintf(f, " voice_activity_detection");
  }
  if (effects & AE_NEGATE) {
    fprintf(f, " negate");
  }
  if (effects & AE_NOISE_CANCELLATION) {
    fprintf(f, " noise_cancellation");
  }
  if (effects & AE_STYLE_TRANSFER) {
    fprintf(f, " style_transfer");
  }
  if (effects & AE_PROCESSOR_OVERRIDDEN) {
    fprintf(f, " processor_overridden");
  }
}

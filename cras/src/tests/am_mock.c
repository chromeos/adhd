/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stddef.h>
#include <stdlib.h>

#include "cras/src/dsp/am.h"

struct am_context {
  int16_t fake_output_value;
};

struct am_context* am_new(const char* model_path) {
  struct am_context* am =
      (struct am_context*)calloc(1, sizeof(struct am_context));
  am->fake_output_value = 1;
  return am;
}

void am_free(struct am_context* am) {
  free(am);
}

/* Fills fake values into the outputs.
 *
 * The filled values start from 1 / 32768. and will be increased once per
 * invocation. The filled values will be reset to 1 / 32768. once it's going to
 * be greater than 32767.
 *
 * In other words, the filled values for each invocation are
 * 1 / 32768., 2 / 32768., 3 / 32768., ... 32767 / 32768., 1 / 32768., ...
 * After converted to int16_t, the values will be 1, 2, 3, ..., 32767, 1, ...
 *
 * Args:
 *    am - the am context.
 *    inputs - the inputs to the audio model.
 *    num_inputs - the number of inputs.
 *    outputs - the buffer to save the outputs.
 *    num_outputs - the number of outputs.
 * Returns:
 *    Always 0.
 */
int am_process(struct am_context* am,
               const float* inputs,
               size_t num_inputs,
               float* outputs,
               size_t num_outputs) {
  for (int i = 0; i < num_outputs; ++i) {
    outputs[i] = am->fake_output_value / 32768.f;
  }

  if (am->fake_output_value < INT16_MAX) {
    am->fake_output_value += 1;
  } else {
    am->fake_output_value = 1;
  }

  return 0;
}

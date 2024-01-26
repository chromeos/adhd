// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "abs_plugin.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

struct abs_processor {
  struct plugin_processor p;
  size_t frame_rate;
};

static enum status abs_processor_run(struct plugin_processor* p,
                                     const struct multi_slice* input,
                                     struct multi_slice* output) {
  for (size_t ch = 0; ch < input->channels; ch++) {
    float* in_ch = input->data[ch];

    for (size_t i = 0; i < input->num_frames; i++) {
      in_ch[i] = fabsf(in_ch[i]);
    }
  }

  *output = *input;
  return StatusOk;
}

static enum status abs_processor_destroy(struct plugin_processor* p) {
  if (!p) {
    return ErrInvalidProcessor;
  }

  free(p);
  return StatusOk;
}

static enum status abs_processor_get_output_frame_rate(
    struct plugin_processor* p,
    size_t* frame_rate) {
  if (!p) {
    return ErrInvalidProcessor;
  }
  struct abs_processor* abs_p = (struct abs_processor*)(p);
  *frame_rate = abs_p->frame_rate;
  return StatusOk;
}

static const struct plugin_processor_ops ops = {
    .run = abs_processor_run,
    .destroy = abs_processor_destroy,
    .get_output_frame_rate = abs_processor_get_output_frame_rate,
};

enum status abs_processor_create(struct plugin_processor** out,
                                 const struct plugin_processor_config* config) {
  struct abs_processor* abs_p = calloc(1, sizeof(*abs_p));
  if (!abs_p) {
    return ErrOutOfMemory;
  }

  abs_p->p.ops = &ops;
  abs_p->frame_rate = config->frame_rate;

  *out = &abs_p->p;
  return StatusOk;
}

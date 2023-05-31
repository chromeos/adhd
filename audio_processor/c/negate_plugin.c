// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "negate_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct negate_processor {
  struct plugin_processor p;
  struct plugin_processor_config config;
  // Buffer for output data.
  // Ideally we'd be using a 1D array. But a 2D array allows address sanitizer
  // to catch programming errors.
  float* buffers[MULTI_SLICE_MAX_CH];
};

static enum status negate_processor_run(struct plugin_processor* p,
                                        const struct multi_slice* input,
                                        struct multi_slice* output) {
  if (!p) {
    return ErrInvalidProcessor;
  }

  struct negate_processor* np = (struct negate_processor*)p;

  if (np->config.debug) {
    fprintf(stderr, "%s() called\n", __func__);
  }

  for (size_t ch = 0; ch < input->channels; ch++) {
    float* in_ch = input->data[ch];
    float* out_ch = np->buffers[ch];

    for (size_t i = 0; i < input->num_frames; i++) {
      out_ch[i] = -1 * in_ch[i];
    }
  }

  output->channels = input->channels;
  output->num_frames = input->num_frames;
  memcpy(output->data, np->buffers, sizeof(output->data));
  return StatusOk;
}

static enum status negate_processor_destroy(struct plugin_processor* p) {
  if (!p) {
    return ErrInvalidProcessor;
  }

  struct negate_processor* np = (struct negate_processor*)p;

  if (np->config.debug) {
    fprintf(stderr, "%s() called\n", __func__);
  }

  for (size_t i = 0; i < np->config.channels; i++) {
    free(np->buffers[i]);
  }

  free(np);
  return StatusOk;
}

enum status negate_processor_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config) {
  static const struct plugin_processor_ops ops = {
      .run = negate_processor_run,
      .destroy = negate_processor_destroy,
  };

  if (config->debug) {
    fprintf(stderr, "%s() called\n", __func__);
  }

  if (config->channels > MULTI_SLICE_MAX_CH) {
    return ErrInvalidConfig;
  }

  struct negate_processor* np = calloc(1, sizeof(*np));
  if (!np) {
    return ErrOutOfMemory;
  }

  np->p.ops = &ops;
  np->config = *config;

  for (size_t i = 0; i < config->channels; i++) {
    np->buffers[i] = calloc(config->block_size, sizeof(*np->buffers[i]));
    if (!np->buffers[i]) {
      negate_processor_destroy(&np->p);
      return ErrOutOfMemory;
    }
  }

  *out = &np->p;
  return StatusOk;
}

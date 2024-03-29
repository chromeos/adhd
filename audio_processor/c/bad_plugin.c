// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bad_plugin.h"

#include <stdlib.h>

#include "audio_processor/c/plugin_processor.h"

static enum status noop_run(struct plugin_processor* p,
                            const struct multi_slice* in,
                            struct multi_slice* out) {
  (void)p;
  *out = *in;
  return StatusOk;
}

static enum status failing_run(struct plugin_processor* p,
                               const struct multi_slice* in,
                               struct multi_slice* out) {
  (void)p;
  (void)in;
  (void)out;
  return ErrOther;
}

static enum status get_output_frame_rate_48k(struct plugin_processor* p,
                                             size_t* frame_rate) {
  *frame_rate = 48000;
  return StatusOk;
}

static enum status failing_get_output_frame_rate(struct plugin_processor* p,
                                                 size_t* frame_rate) {
  return ErrOther;
}

static enum status free_destroy(struct plugin_processor* p) {
  free(p);
  return StatusOk;
}

enum status bad_plugin_oom_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config) {
  (void)out;
  (void)config;
  return ErrOutOfMemory;
}

enum status bad_plugin_null_processor_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config) {
  (void)config;
  *out = NULL;
  return StatusOk;
}

enum status bad_plugin_null_ops_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config) {
  (void)config;

  static struct plugin_processor p = {
      .ops = NULL,
  };
  *out = &p;
  return StatusOk;
}

enum status bad_plugin_missing_run_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config) {
  (void)config;
  static const struct plugin_processor_ops ops = {
      .run = NULL,
      .destroy = free_destroy,
      .get_output_frame_rate = get_output_frame_rate_48k,
  };

  struct plugin_processor* p = calloc(1, sizeof(*p));
  p->ops = &ops;
  *out = p;
  return StatusOk;
}

enum status bad_plugin_missing_destroy_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config) {
  (void)config;
  static const struct plugin_processor_ops ops = {
      .run = noop_run,
      .destroy = NULL,
      .get_output_frame_rate = get_output_frame_rate_48k,
  };

  static struct plugin_processor p = {
      .ops = &ops,
  };
  *out = &p;
  return StatusOk;
}

enum status bad_plugin_missing_get_output_frame_rate_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config) {
  (void)config;
  static const struct plugin_processor_ops ops = {
      .run = noop_run,
      .destroy = free_destroy,
      .get_output_frame_rate = NULL,
  };

  struct plugin_processor* p = calloc(1, sizeof(*p));
  p->ops = &ops;
  *out = p;
  return StatusOk;
}

enum status bad_plugin_failing_run_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config) {
  (void)config;
  static const struct plugin_processor_ops ops = {
      .run = failing_run,
      .destroy = free_destroy,
      .get_output_frame_rate = get_output_frame_rate_48k,
  };

  struct plugin_processor* p = calloc(1, sizeof(*p));
  p->ops = &ops;
  *out = p;
  return StatusOk;
}

enum status bad_plugin_failing_get_output_frame_rate_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config) {
  (void)config;
  static const struct plugin_processor_ops ops = {
      .run = failing_run,
      .destroy = free_destroy,
      .get_output_frame_rate = failing_get_output_frame_rate,
  };

  struct plugin_processor* p = calloc(1, sizeof(*p));
  p->ops = &ops;
  *out = p;
  return StatusOk;
}

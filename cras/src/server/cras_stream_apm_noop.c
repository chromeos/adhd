/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>

#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_stream_apm.h"
#include "cras/src/server/float_buffer.h"

/*
 * If webrtc audio processing library is not available then define all
 * cras_stream_apm functions as empty. As long as cras_stream_apm_add returns
 * NULL, non of the other functions should be called.
 */
int cras_stream_apm_init(const char* device_config_dir) {
  return 0;
}
void cras_stream_apm_reload_aec_config() {}
int cras_stream_apm_deinit() {
  return 0;
}
struct cras_stream_apm* cras_stream_apm_create(uint64_t effects) {
  return NULL;
}
struct cras_apm* cras_stream_apm_add(struct cras_stream_apm* stream,
                                     struct cras_iodev* idev,
                                     const struct cras_audio_format* fmt) {
  return NULL;
}
struct cras_apm* cras_stream_apm_get_active(struct cras_stream_apm* stream,
                                            const struct cras_iodev* idev) {
  return NULL;
}
void cras_stream_apm_start(struct cras_stream_apm* stream,
                           const struct cras_iodev* idev) {}

void cras_stream_apm_stop(struct cras_stream_apm* stream,
                          struct cras_iodev* idev) {}
uint64_t cras_stream_apm_get_effects(struct cras_stream_apm* stream) {
  return 0;
}

int cras_stream_apm_destroy(struct cras_stream_apm* stream) {
  return 0;
}
void cras_stream_apm_remove(struct cras_stream_apm* stream,
                            const struct cras_iodev* idev) {}

int cras_stream_apm_process(struct cras_apm* apm,
                            struct float_buffer* input,
                            unsigned int offset,
                            float preprocessing_gain_scalar) {
  return 0;
}

struct cras_audio_area* cras_stream_apm_get_processed(struct cras_apm* apm) {
  return NULL;
}

void cras_stream_apm_put_processed(struct cras_apm* apm, unsigned int frames) {}

struct cras_audio_format* cras_stream_apm_get_format(struct cras_apm* apm) {
  return NULL;
}

bool cras_stream_apm_get_use_tuned_settings(struct cras_stream_apm* stream,
                                            const struct cras_iodev* idev) {
  return 0;
}

void cras_stream_apm_set_aec_dump(struct cras_stream_apm* stream,
                                  const struct cras_iodev* idev,
                                  int start,
                                  int fd) {}

int cras_stream_apm_set_aec_ref(struct cras_stream_apm* stream,
                                struct cras_iodev* echo_ref) {
  return 0;
}

void cras_stream_apm_notify_vad_target_changed(
    struct cras_stream_apm* vad_target) {}

int cras_stream_apm_message_handler_init() {
  return 0;
}

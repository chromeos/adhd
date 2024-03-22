// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/server/cras_thread.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <threads.h>

#include "cras/common/check.h"

static thread_local bool main_ctx_allowed = false;
static thread_local bool audio_ctx_allowed = false;

struct cras_main_ctx* checked_main_ctx() {
  CRAS_CHECK(main_ctx_allowed);

  static struct cras_main_ctx mctx = {};
  return &mctx;
}

struct cras_audio_ctx* checked_audio_ctx() {
  CRAS_CHECK(audio_ctx_allowed);

  static struct cras_audio_ctx actx = {};
  return &actx;
}

void cras_thread_init_main() {
  main_ctx_allowed = audio_ctx_allowed = true;
}

struct start_routine_wrapper_data {
  void* (*start_routine)(void*);
  void* arg;
};

static void* start_routine_wrapper(void* arg) {
  main_ctx_allowed = false;
  audio_ctx_allowed = true;
  struct start_routine_wrapper_data data =
      *(struct start_routine_wrapper_data*)arg;
  free(arg);
  return data.start_routine(data.arg);
}

int cras_thread_create_audio(pthread_t* thread,
                             const pthread_attr_t* attr,
                             void* (*start_routine)(void*),
                             void* arg) {
  struct start_routine_wrapper_data* data =
      calloc(sizeof(struct start_routine_wrapper_data), 1);
  data->start_routine = start_routine;
  data->arg = arg;
  int rc = pthread_create(thread, attr, start_routine_wrapper, data);

  if (rc != 0) {
    free(data);
    return rc;
  }

  // Block accessing audio_ctx on the main thread if the audio thread was
  // created successfully.
  audio_ctx_allowed = false;
  return 0;
}

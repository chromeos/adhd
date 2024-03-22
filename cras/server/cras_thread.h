// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SERVER_CRAS_THREAD_H_
#define CRAS_SERVER_CRAS_THREAD_H_

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// Main thread context. Singleton.
struct cras_main_ctx {
  int test_num;
};

// Audio thread context. Singleton.
struct cras_audio_ctx {
  int test_num;
};

// Returns the main thread context singleton if the current thread is the main
// thread.
// Otherwise aborts (SIGABRT) the program.
struct cras_main_ctx* checked_main_ctx();

// Returns the audio thread context singleton if any of the following is true:
// - The current thread is the audio thread
// - The audio thread has not started yet.
//   This is allowed to ease initialization. If you acquire the context this
//   way you should not store the acquired context.
// Otherwise aborts (SIGABRT) the program.
struct cras_audio_ctx* checked_audio_ctx();

// Registers the current thread as the main thread.
void cras_thread_init_main();

// Wrapper to create the audio thread.
int cras_thread_create_audio(pthread_t* thread,
                             const pthread_attr_t* attr,
                             void* (*start_routine)(void*),
                             void* arg);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SERVER_CRAS_THREAD_H_

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_SERVER_SIDETONE_H_
#define CRAS_SRC_SERVER_SIDETONE_H_

#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

struct stream_list;
struct cras_rstream;

// Create the sidetone input and output streams.
// Returns true if success.
bool enable_sidetone(struct stream_list* stream_list);

// Destroy the sidetone input and output streams.
void disable_sidetone(struct stream_list* stream_list);

// Merge the input and output shm and assign the cras_rstream->pair field.
void configure_sidetone_streams(struct cras_rstream* input,
                                struct cras_rstream* output);

// Get the maximum allowed callback level based on the frame rate.
static inline unsigned int sidetone_get_max_cb_level(size_t frame_rate) {
  // Number of frames within 10 ms
  return frame_rate / 100;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_SIDETONE_H_

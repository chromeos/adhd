/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_TESTS_SR_STUB_H_
#define CRAS_SRC_TESTS_SR_STUB_H_

extern "C" {
#include "cras/src/server/cras_sr.h"

// The original cras_sr.h is included.
// The following functions are added for testing.

// Sets the frames_ratio field of the sr instance.
void cras_sr_set_frames_ratio(struct cras_sr* sr, double frames_ratio);

// Sets the frames_ratio field of the sr instance.
void cras_sr_set_num_frames_per_run(struct cras_sr* sr,
                                    size_t num_frames_per_run);
}

#endif  // CRAS_SRC_TESTS_SR_STUB_H_
/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_LEA_IODEV_H_
#define CRAS_SRC_SERVER_CRAS_LEA_IODEV_H_

#include <stdint.h>

#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates a lea iodev representing a group.
 * Note that if a group supports both input and output, two `lea_iodev`s
 * will be instantiated.
 * Args:
 *    lea - The associated |cras_lea| object.
 *    name - Name associated to the LE audio group.
 *    group - ID of the associated group.
 *    dir - The direction of the iodev.
 */
struct cras_iodev* lea_iodev_create(struct cras_lea* lea,
                                    const char* name,
                                    int group_id,
                                    enum CRAS_STREAM_DIRECTION dir);

// Destroys a lea iodev.
void lea_iodev_destroy(struct cras_iodev* iodev);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_LEA_IODEV_H_

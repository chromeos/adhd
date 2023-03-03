/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_DSP_QUAD_ROTATION_H_
#define CRAS_SRC_DSP_QUAD_ROTATION_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "cras/src/server/cras_dsp_pipeline.h"
#include "cras_iodev_info.h"

// In clockwise order
enum SPEAKER_POSITION {
  SPK_POS_FL,
  SPK_POS_RL,
  SPK_POS_RR,
  SPK_POS_FR,
  NUM_SPEAKER_POS_QUAD,
};

enum DIRECTION {
  CLOCK_WISE,
  ANTI_CLOCK_WISE,
};

struct quad_rotation {
  enum CRAS_SCREEN_ROTATION rotation;
  /* The speaker position to port map. Ex: port_map[SPK_POS_FL] = x; The data
   * of SPK_POS_FL is in the x port. This map needs to be initialize to set
   * port_map = (0, 1, 2, 3) */
  int port_map[NUM_SPEAKER_POS_QUAD];
  float* ports[8];
  float buf[DSP_BUFFER_SIZE];
};

/* Swaps data on x and y channel for Quad channel audio.
 * Args:
 *    quad_rotation - The pointer of struct quad_rotation.
 *    x     - The channel to be swapped.
 *    y     - The channel to be swapped.
 *    samples - The number of samples to convert.
 */
void quad_rotation_swap(struct quad_rotation* data,
                        enum SPEAKER_POSITION x,
                        enum SPEAKER_POSITION y,
                        unsigned long samples);

/* Rotate the Quad channel audio for 90 degrees.
 * Args:
 *    quad_rotation - The pointer of struct quad_rotation.
 *    direction - The rotation direction.
 *    samples - The number of samples to convert.
 */
void quad_rotation_rotate_90(struct quad_rotation* data,
                             enum DIRECTION direction,
                             unsigned long samples);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // CRAS_SRC_DSP_QUAD_ROTATION_H_
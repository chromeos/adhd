/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_MIX_OPS_H_
#define CRAS_SRC_SERVER_CRAS_MIX_OPS_H_

#include <stdint.h>

#include "cras/src/server/cras_system_state.h"

extern const struct cras_mix_ops mixer_ops;
extern const struct cras_mix_ops mixer_ops_sse42;
extern const struct cras_mix_ops mixer_ops_avx;
extern const struct cras_mix_ops mixer_ops_avx2;
extern const struct cras_mix_ops mixer_ops_fma;

/* Struct containing ops to implement mix/scale on a buffer of samples.
 * Different architecture can provide different implementations and wraps
 * the implementations into cras_mix_ops.
 * Different sample formats will be handled by different implementations.
 * The usage of each operation is explained in cras_mix.h
 */
struct cras_mix_ops {
  // See cras_scale_buffer_increment.
  void (*scale_buffer_increment)(snd_pcm_format_t fmt,
                                 uint8_t* buff,
                                 unsigned int count,
                                 float scaler,
                                 float increment,
                                 float target,
                                 int step);
  // See cras_scale_buffer.
  void (*scale_buffer)(snd_pcm_format_t fmt,
                       uint8_t* buff,
                       unsigned int count,
                       float scaler);
  // See cras_mix_add.
  void (*add)(snd_pcm_format_t fmt,
              uint8_t* dst,
              uint8_t* src,
              unsigned int count,
              unsigned int index,
              int mute,
              float mix_vol);
  // See cras_mix_add_scale_stride.
  void (*add_scale_stride)(snd_pcm_format_t fmt,
                           uint8_t* dst,
                           uint8_t* src,
                           unsigned int count,
                           unsigned int dst_stride,
                           unsigned int src_stride,
                           float scaler);
  // cras_mix_mute_buffer.
  size_t (*mute_buffer)(uint8_t* dst, size_t frame_bytes, size_t count);
};
#endif

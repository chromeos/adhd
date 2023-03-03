/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/dsp/quad_rotation.h"

void quad_rotation_swap(struct quad_rotation* data,
                        enum SPEAKER_POSITION x,
                        enum SPEAKER_POSITION y,
                        unsigned long samples) {
  float** ports;
  ports = (float**)data->ports;
  const size_t size = samples * sizeof(float);

  memcpy(data->buf, ports[data->port_map[x]], size);
  memcpy(ports[data->port_map[x]], ports[data->port_map[y]], size);
  memcpy(ports[data->port_map[y]], data->buf, size);
}

void quad_rotation_rotate_90(struct quad_rotation* data,
                             enum DIRECTION direction,
                             unsigned long samples) {
  float** ports = (float**)data->ports;
  int i = 0;
  const int step = (direction == CLOCK_WISE) ? 1 : -1;
  float* to = data->buf;
  const size_t size = samples * sizeof(float);
  do {
    memcpy(to, ports[data->port_map[i]], size);
    to = ports[data->port_map[i]];
    // (x + y) & 0x3 = (x + y) mod 4
    i = (i + step) & 0x3;
  } while (i != 0);
  memcpy(to, data->buf, size);
}

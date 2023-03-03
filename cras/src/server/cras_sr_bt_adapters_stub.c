/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_sr.h"
#include "cras/src/server/cras_sr_bt_adapters.h"
#include "cras/src/server/cras_sr_bt_util.h"

struct cras_iodev_sr_bt_adapter {};

struct cras_iodev_sr_bt_adapter* cras_iodev_sr_bt_adapter_create(
    struct cras_iodev* iodev,
    struct cras_sr* sr) {
  return NULL;
}

void cras_iodev_sr_bt_adapter_destroy(
    struct cras_iodev_sr_bt_adapter* adapter){};

int cras_iodev_sr_bt_adapter_frames_queued(
    struct cras_iodev_sr_bt_adapter* adapter,
    struct timespec* tstamp) {
  return 0;
};

int cras_iodev_sr_bt_adapter_delay_frames(
    struct cras_iodev_sr_bt_adapter* adapter) {
  return 0;
};

int cras_iodev_sr_bt_adapter_get_buffer(
    struct cras_iodev_sr_bt_adapter* adapter,
    struct cras_audio_area** area,
    unsigned* frames) {
  return 0;
};

int cras_iodev_sr_bt_adapter_put_buffer(
    struct cras_iodev_sr_bt_adapter* adapter,
    const unsigned nread) {
  return 0;
};

int cras_iodev_sr_bt_adapter_flush_buffer(
    struct cras_iodev_sr_bt_adapter* adapter) {
  return 0;
};

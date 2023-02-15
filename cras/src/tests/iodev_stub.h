/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_TESTS_IODEV_STUB_H_
#define CRAS_SRC_TESTS_IODEV_STUB_H_

#include <time.h>

void iodev_stub_reset();

void iodev_stub_est_rate_ratio(cras_iodev* iodev, double ratio);

void iodev_stub_update_rate(cras_iodev* iodev, int data);

void iodev_stub_on_internal_card(cras_ionode* node, int data);

void iodev_stub_frames_queued(cras_iodev* iodev, int ret, timespec ts);

void iodev_stub_valid_frames(cras_iodev* iodev, int ret, timespec ts);

bool iodev_stub_get_drop_time(cras_iodev* iodev, timespec* ts);

#endif  // CRAS_SRC_TESTS_IODEV_STUB_H_

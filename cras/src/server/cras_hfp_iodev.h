/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_HFP_IODEV_H_
#define CRAS_SRC_SERVER_CRAS_HFP_IODEV_H_

#include "cras/src/server/cras_bt_device.h"
#include "cras/src/server/cras_sco.h"
#include "cras_types.h"

struct hfp_slc_handle;

/*
 * Creates an hfp iodev.
 */
struct cras_iodev* hfp_iodev_create(enum CRAS_STREAM_DIRECTION dir,
                                    struct cras_bt_device* device,
                                    struct hfp_slc_handle* slc,
                                    struct cras_sco* sco);

void hfp_iodev_destroy(struct cras_iodev* iodev);

#endif  // CRAS_SRC_SERVER_CRAS_HFP_IODEV_H_

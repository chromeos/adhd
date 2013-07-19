/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_HFP_IODEV_H_
#define CRAS_HFP_IODEV_H_

#include "cras_bt_transport.h"
#include "cras_hfp_info.h"
#include "cras_types.h"


/*
 * Creates an hfp iodev.
 */
struct cras_iodev *hfp_iodev_create(
		enum CRAS_STREAM_DIRECTION dir,
		struct cras_bt_transport *transport,
		struct hfp_info *info);

void hfp_iodev_destroy(struct cras_iodev *iodev);

#endif /* CRAS_HFP_IODEV_H_ */

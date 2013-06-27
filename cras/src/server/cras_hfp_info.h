/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_HFP_INFO_H_
#define CRAS_HFP_INFO_H_

#include "cras_iodev.h"
#include "cras_types.h"


/* Structure to handle sample transmission between CRAS and the SCO
 * socket acquired from bluez.
 */
struct hfp_info;

/* Creates an hfp_info instance. */
struct hfp_info *hfp_info_create();

/* Destroys given hfp_info instance. */
void hfp_info_destroy(struct hfp_info *info);

/* Adds cras_iodev to given hfp_info.  Only when an output iodev is added,
 * hfp_info starts sending samples to the SCO socket. Similarly, only when an
 * input iodev is added, it starts to read samples from SCO socket.
 */
int hfp_info_add_iodev(struct hfp_info *info, struct cras_iodev *dev);

/* Removes cras_iodev from hfp_info.  hfp_info will stop sending or
 * reading samples right after the iodev is removed. This function is used for
 * iodev closure.
 */
int hfp_info_rm_iodev(struct hfp_info *info, struct cras_iodev *dev);

/* Checks if there's any iodev added to the given hfp_info. */
int hfp_info_has_iodev(struct hfp_info *info);

#endif /* CRAS_HFP_INFO_H_ */

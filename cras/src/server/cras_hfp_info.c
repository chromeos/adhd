/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdlib.h>

#include "cras_hfp_info.h"


struct hfp_info {
	struct cras_iodev *idev;
	struct cras_iodev *odev;
};

int hfp_info_add_iodev(struct hfp_info *info, struct cras_iodev *dev)
{
	if (dev->direction == CRAS_STREAM_OUTPUT) {
		if (info->odev)
			goto invalid;
		info->odev = dev;
	} else if (dev->direction == CRAS_STREAM_INPUT) {
		if (info->idev)
			goto invalid;
		info->idev = dev;
	}

	return 0;

invalid:
	return -EINVAL;
}

int hfp_info_rm_iodev(struct hfp_info *info, struct cras_iodev *dev)
{
	if (dev->direction == CRAS_STREAM_OUTPUT && info->odev == dev) {
		info->odev = NULL;
	} else if (dev->direction == CRAS_STREAM_INPUT && info->idev == dev){
		info->idev = NULL;
	} else
		return -EINVAL;

	return 0;
}

int hfp_info_has_iodev(struct hfp_info *info)
{
	return info->odev || info->idev;
}

struct hfp_info *hfp_info_create()
{
	struct hfp_info *info;
	info = (struct hfp_info *)calloc(1, sizeof(*info));

	return info;
}

void hfp_info_destroy(struct hfp_info *info)
{
	free(info);
}

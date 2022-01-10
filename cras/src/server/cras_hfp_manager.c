/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>
#include <syslog.h>

#include "cras_fl_media.h"
#include "cras_fl_pcm_iodev.h"
#include "cras_types.h"

/*
 * Object holding information and resources of a connected HFP headset.
 * Members:
 *    fm - Object representing the media interface of BT adapter.
 *    idev - The input iodev for HFP.
 *    odev - The output iodev for HFP.
 *    addr - The address of connected a2dp device.
 */
struct cras_hfp {
	struct fl_media *fm;
	struct cras_iodev *idev;
	struct cras_iodev *odev;
	char *addr;
};

static struct cras_hfp *connected_hfp = NULL;

/* Creates cras_hfp object representing a connected hfp device. */
struct cras_hfp *cras_floss_hfp_create(struct fl_media *fm, const char *addr)
{
	if (connected_hfp) {
		syslog(LOG_ERR, "Hfp already connected");
		return NULL;
	}
	connected_hfp = (struct cras_hfp *)calloc(1, sizeof(*connected_hfp));

	connected_hfp->fm = fm;
	connected_hfp->addr = strdup(addr);
	connected_hfp->idev =
		hfp_pcm_iodev_create(connected_hfp, CRAS_STREAM_INPUT);
	connected_hfp->odev =
		hfp_pcm_iodev_create(connected_hfp, CRAS_STREAM_OUTPUT);

	return connected_hfp;
}

/* Destroys given cras_hfp object. */
void cras_floss_hfp_destroy(struct cras_hfp *hfp)
{
	if (hfp->idev)
		hfp_pcm_iodev_destroy(hfp->idev);
	if (hfp->odev)
		hfp_pcm_iodev_destroy(hfp->odev);
	if (hfp->addr)
		free(hfp->addr);
	free(hfp);
	connected_hfp = NULL;
}

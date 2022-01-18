/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_HFP_MANAGER_H_
#define CRAS_HFP_MANAGER_H_

struct cras_hfp;
struct fl_media;

/* Creates cras_hfp object representing a connected hfp device. */
struct cras_hfp *cras_floss_hfp_create(struct fl_media *fm, const char *addr);

/* Checks if given cras_hfp is started. */
int cras_floss_hfp_started(struct cras_hfp *hfp);

/* Gets the file descriptor to read/write to given cras_hfp.
 * Returns -1 if given cras_hfp isn't started. */
int cras_floss_hfp_get_fd(struct cras_hfp *hfp);

/* Starts hfp streaming on given cras_hfp.
 * Returns 0 for success, otherwise error code. */
int cras_floss_hfp_start(struct cras_hfp *hfp);

/* Stops hfp streaming. */
int cras_floss_hfp_stop(struct cras_hfp *hfp);

/* Destroys given cras_hfp object. */
void cras_floss_hfp_destroy(struct cras_hfp *hfp);

#endif /* CRAS_HFP_MANAGER_H_ */

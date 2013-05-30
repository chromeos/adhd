/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_HFP_SLC_H_
#define CRAS_HFP_SLC_H_

struct hfp_slc_handle;

/* Callback for service level connection initialized */
typedef int (*hfp_slc_init_cb)(struct hfp_slc_handle *handle, void *cb_data);

/* Creates an hfp_slc_handle to poll the RFCOMM file descriptor
 * to read and handle received AT commands.
 * Args:
 *    fd - the rfcomm fd used to initialize service level connection
 *    cb - the callback function to be triggered when a service level
 *        connection is initialized.
 *    cb_data - data to be passed to the hfp_slc_init_cb function.
 */
struct hfp_slc_handle *hfp_slc_create(int fd, hfp_slc_init_cb cb,
				      void *cb_data);

/* Destroys an hfp_slc_handle. */
void hfp_slc_destroy(struct hfp_slc_handle *handle);

#endif /* CRAS_HFP_SLC_H_ */

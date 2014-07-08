/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_HFP_SLC_H_
#define CRAS_HFP_SLC_H_

struct hfp_slc_handle;

/* Callback to call when service level connection initialized. */
typedef int (*hfp_slc_init_cb)(struct hfp_slc_handle *handle, void *cb_data);

/* Callback to call when service level connection disconnected. */
typedef int (*hfp_slc_disconnect_cb)(struct hfp_slc_handle *handle);

/* Creates an hfp_slc_handle to poll the RFCOMM file descriptor
 * to read and handle received AT commands.
 * Args:
 *    fd - the rfcomm fd used to initialize service level connection
 *    init_cb - the callback function to be triggered when a service level
 *        connection is initialized.
 *    init_cb_data - data to be passed to the hfp_slc_init_cb function.
 *    disconnect_cb - the callback function to be triggered when the service
 *        level connection is disconnected.
 */
struct hfp_slc_handle *hfp_slc_create(int fd, hfp_slc_init_cb init_cb,
				      void *init_cb_data,
				      hfp_slc_disconnect_cb disconnect_cb);

/* Destroys an hfp_slc_handle. */
void hfp_slc_destroy(struct hfp_slc_handle *handle);

/* Gets the active SLC handle, for qualification test. */
struct hfp_slc_handle *hfp_slc_get_handle();

/* Fakes the answer call event for qualification test. */
int hfp_event_answer_call(struct hfp_slc_handle *handle);

/* Fakes the incoming call event for qualification test. */
int hfp_event_incoming_call(struct hfp_slc_handle *handle,
			    const char *number,
			    int type);

/* Fakes the terminate call event for qualification test. */
int hfp_event_terminate_call(struct hfp_slc_handle *handle);

/* Mocks the state that a last dialed number stored in memory. */
int hfp_event_store_dial_number(struct hfp_slc_handle *handle, const char *num);

/* Sets battery level which is required for qualification test. */
int hfp_event_set_battery(struct hfp_slc_handle *handle, int value);

/* Sets signal strength which is required for qualification test. */
int hfp_event_set_signal(struct hfp_slc_handle *handle, int value);

/* Sets service availability which is required for qualification test. */
int hfp_event_set_service(struct hfp_slc_handle *handle, int value);

#endif /* CRAS_HFP_SLC_H_ */

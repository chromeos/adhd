/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_HFP_MANAGER_H_
#define CRAS_SRC_SERVER_CRAS_HFP_MANAGER_H_

#include <stdbool.h>

#include "cras/src/server/audio_thread.h"
#include "cras_types.h"

struct cras_hfp;
struct fl_media;

// Creates cras_hfp object representing a connected hfp device.
struct cras_hfp* cras_floss_hfp_create(struct fl_media* fm,
                                       const char* addr,
                                       const char* name,
                                       bool wbs_supported);

/* Starts hfp streaming on given cras_hfp for the specified direction.
 * Returns 0 for success, otherwise error code. */
int cras_floss_hfp_start(struct cras_hfp* hfp,
                         thread_callback cb,
                         enum CRAS_STREAM_DIRECTION dir);

// Stops hfp streaming for the specified direction.
int cras_floss_hfp_stop(struct cras_hfp* hfp, enum CRAS_STREAM_DIRECTION dir);

/* Gets the file descriptor to read/write to given cras_hfp.
 * Returns -1 if given cras_hfp isn't started. */
int cras_floss_hfp_get_fd(struct cras_hfp* hfp);

// Gets the input iodev attached to the given cras_hfp.
struct cras_iodev* cras_floss_hfp_get_input_iodev(struct cras_hfp* hfp);

// Gets the output iodev attached to the given cras_hfp.
struct cras_iodev* cras_floss_hfp_get_output_iodev(struct cras_hfp* hfp);

// Gets the human readable name of the hfp device.
const char* cras_floss_hfp_get_display_name(struct cras_hfp* hfp);

// Gets the address of the hfp device.
const char* cras_floss_hfp_get_addr(struct cras_hfp* hfp);

// Gets the stable id of the hfp device.
const uint32_t cras_floss_hfp_get_stable_id(struct cras_hfp* hfp);

// Set the volume of the hfp device.
void cras_floss_hfp_set_volume(struct cras_hfp* hfp, unsigned int volume);

/* Check if HFP audio was disconnected by the headset.
 * If true, issue a switch-profile event as a means to reconnect. */
void cras_floss_hfp_possibly_reconnect(struct cras_hfp* hfp);

// Fills the format property lists.
int cras_floss_hfp_fill_format(struct cras_hfp* hfp,
                               size_t** rates,
                               snd_pcm_format_t** formats,
                               size_t** channel_counts);

/* Convert the HFP speaker volume received from the headset's volume change
 * event to CRAS's system volume. */
int cras_floss_hfp_convert_volume(unsigned int vgs_volume);

// Get if HFP wideband speech is supported.
bool cras_floss_hfp_get_wbs_supported(struct cras_hfp* hfp);

// Destroys given cras_hfp object.
void cras_floss_hfp_destroy(struct cras_hfp* hfp);

#endif  // CRAS_SRC_SERVER_CRAS_HFP_MANAGER_H_

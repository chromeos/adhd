/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_APM_REVERSE_H_
#define CRAS_APM_REVERSE_H_

#include "cras_types.h"

struct cras_iodev;
struct float_buffer;

/* Interface for audio processing function called in the context of an
 * reverse module from the DSP pipeline of cras_iodev.
 * Args:
 *    fbuf - Holds the deinterleaved audio data for AEC processing.
 *    frame_rate - The frame rate the audio data is in.
 */
typedef int (*process_reverse_t)(struct float_buffer *fbuf,
				 unsigned int frame_rate);

/* Function to check the conditions and then determine if APM reverse
 * processing is needed.
 */
typedef int (*process_reverse_needed_t)();

/* Initializes APM reverse module with utility functions passed in.
 *
 * APM reverse module is designed to provide stable and correct routing of
 * audio output(playback) data to the APM instances on caller side.
 * The reverse data routing is subject to:
 * (1) The overall stream effects requirement from APM list side.
 * (2) The dynamically changing default audio output device.
 *
 * Args:
 *    process_cb - Function provided by caller for the actual processing.
 *        The reverse(playback) data goes through the DSP pipeline and
 *        eventually passes to this callback function in audio thread.
 *    process_needed_cb - Function provided by caller to check if reverse
 *        processing is needed. APM reverse module uses this to re-sync
 *        states with APM lists in main thread.
 */
int cras_apm_reverse_init(process_reverse_t process_cb,
			  process_reverse_needed_t process_needed_cb);

/* Notifies important state changes to APM reverse module so it handles
 * changes internally and synchronize the states with APM list.
 */
void cras_apm_reverse_state_update();

/* Returns if the audio output devices configuration meets our AEC use
 * case. */
bool cras_apm_reverse_is_aec_use_case();

/* Deinitializes APM reverse modules and all related resources. */
void cras_apm_reverse_deinit();

#endif /* CRAS_APM_LIST_H_ */

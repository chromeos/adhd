/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_APM_REVERSE_H_
#define CRAS_SRC_SERVER_CRAS_APM_REVERSE_H_

#include "cras_types.h"

struct cras_iodev;
struct cras_stream_apm;
struct float_buffer;

/* Interface for audio processing function called in the context of an
 * reverse module from the DSP pipeline of cras_iodev in audio thread.
 * Args:
 *    fbuf - Holds the deinterleaved audio data for AEC processing.
 *    frame_rate - The frame rate the audio data is in.
 *    echo_ref - The iodev which passes the audio data to reverse module.
 *        The implementation side can use this information to decide if
 *        an APM wants to do the processing.
 */
typedef int (*process_reverse_t)(struct float_buffer* fbuf,
                                 unsigned int frame_rate,
                                 const struct cras_iodev* echo_ref);

/* Function to check the conditions and then determine if APM reverse
 * processing is needed. Called in audio thread.
 * Args:
 *    default_reverse - If the reverse module calling this callback
 *        is the default one. If true then the |echo_ref| argument is
 *        the current system default audio output.
 *    echo_ref - The echo ref iodev which wants to check if any APM
 *        wants to process reverse data on it.
 */
typedef int (*process_reverse_needed_t)(bool default_reverse,
                                        const struct cras_iodev* echo_ref);

/* Function to be triggered when the output devices has changed and
 * cause APM reverse modules state changes. Called in main thread.
 */
typedef void (*output_devices_changed_t)();

/* Initializes APM reverse module with utility functions passed in. Called
 * in main thread.
 *
 * APM reverse module is designed to provide stable and correct routing of
 * audio output(playback) data to the APM instances on caller side.
 * The reverse data routing is subject to:
 * (1) The overall stream effects requirement from stream APM side.
 * (2) The dynamically changing default audio output device.
 *
 * Args:
 *    process_cb - Function provided by caller for the actual processing.
 *        The reverse(playback) data goes through the DSP pipeline and
 *        eventually passes to this callback function in audio thread.
 *    process_needed_cb - Function provided by caller to check if reverse
 *        processing is needed. APM reverse module uses this to re-sync
 *        states with stream APMs in main thread.
 *    output_devices_changed_cb - Function provided by caller to handle the
 *        event when output devices has changed in reverse modules side.
 */
int cras_apm_reverse_init(process_reverse_t process_cb,
                          process_reverse_needed_t process_needed_cb,
                          output_devices_changed_t output_devices_changed_cb);

/* Notifies important state changes to APM reverse module so it handles
 * changes internally and synchronizes the states with stream APM. Called
 * in audio thread.
 */
void cras_apm_reverse_state_update();

/* Links an iodev as echo ref to stream APM. Called in main thread.
 * Set |echo_ref| to NULL means to remove the linkage information
 * in apm reverse modules.
 * Args:
 *    stream - The stream APM representing the client stream.
 *    echo_ref - The target iodev to be used as echo ref. NULL means
 *        to unlink |stream| with the echo ref that has linked before.
 * Returns:
 *    0 if success, otherwise error code.
 */
int cras_apm_reverse_link_echo_ref(struct cras_stream_apm* stream,
                                   struct cras_iodev* echo_ref);

/* Returns if the audio output devices configuration meets our AEC use
 * case.
 * Args:
 *    echo_ref - The target iodev to check whether it meets AEC use case.
 *        Passing NULL echo_ref means caller doesn't specify any echo
 *        reference device so the check should be made on the default
 *        output device.
 * */
bool cras_apm_reverse_is_aec_use_case(struct cras_iodev* echo_ref);

// Deinitializes APM reverse modules and all related resources.
void cras_apm_reverse_deinit();

#endif  // CRAS_APM_LIST_H_

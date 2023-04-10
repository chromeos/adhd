/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CRAS_SRC_SERVER_CRAS_ALSA_IO_COMMON_H_
#define CRAS_SRC_SERVER_CRAS_ALSA_IO_COMMON_H_

#include <stdbool.h>
#include <sys/time.h>

#include "cras/src/server/cras_alsa_ucm.h"
#include "cras/src/server/cras_iodev.h"

#define HOTWORD_DEV "Wake on Voice"
#define DEFAULT "(default)"
#define HDMI "HDMI"
#define INTERNAL_MICROPHONE "Internal Mic"
#define INTERNAL_SPEAKER "Speaker"
#define KEYBOARD_MIC "Keyboard Mic"
#define HEADPHONE "Headphone"
#define MIC "Mic"
#define USB "USB"
#define LOOPBACK_CAPTURE "Loopback Capture"
#define LOOPBACK_PLAYBACK "Loopback Playback"

/*
 * For USB, pad the output buffer.  This avoids a situation where there isn't a
 * complete URB's worth of audio ready to be transmitted when it is requested.
 * The URB interval does track directly to the audio clock, making it hard to
 * predict the exact interval.
 */
#define USB_EXTRA_BUFFER_FRAMES 768

/*
 * When snd_pcm_avail returns a value that is greater than buffer size,
 * we know there is an underrun. If the number of underrun samples
 * (avail - buffer_size) is greater than SEVERE_UNDERRUN_MS * rate,
 * it is a severe underrun. Main thread should disable and then enable
 * device to recover it from underrun.
 */
#define SEVERE_UNDERRUN_MS 5000

// Default 25 step, volume change 4% once a time
#define NUMBER_OF_VOLUME_STEPS_DEFAULT 25

// maxium 25 step, volume change 4% once a time
#define NUMBER_OF_VOLUME_STEPS_MAX 25

// minium 10 step, volume change 10% once a time
#define NUMBER_OF_VOLUME_STEPS_MIN 10

/*
 * For USB, some of them report invalid volume ranges.
 * Therefore, we need to check the USB volume range is reasonable.
 * Otherwise we fall back to software volume and use the default volume curve.
 * The volume range reported by USB within the range will be valid.
 */

// 5dB
#define VOLUME_RANGE_DB_MIN 5
// 200dB
#define VOLUME_RANGE_DB_MAX 200

// Enumeration for logging to CRAS server metrics.
enum CRAS_NOISE_CANCELLATION_STATUS {
  CRAS_NOISE_CANCELLATION_BLOCKED,
  CRAS_NOISE_CANCELLATION_DISABLED,
  CRAS_NOISE_CANCELLATION_ENABLED,
};

/*
 * When entering no stream state, audio thread needs to fill extra zeros in
 * order to play remaining valid frames. The value indicates how many
 * time will be filled.
 */
static const struct timespec no_stream_fill_zeros_duration = {
    0, 50 * 1000 * 1000  // 50 msec.
};

struct cras_ionode* first_plugged_node(struct cras_iodev* iodev);

// Enable or disable noise cancellation for the active node if supported.
//
// Returns nonzero on unrecoverable failures.
int cras_alsa_common_configure_noise_cancellation(
    struct cras_iodev* iodev,
    struct cras_use_case_mgr* ucm);

// Get the provider for noise cancellation on the node.
enum CRAS_IONODE_NC_PROVIDER cras_alsa_common_get_nc_provider(
    struct cras_use_case_mgr* ucm,
    const char* node_name);

#endif  // CRAS_SRC_SERVER_CRAS_ALSA_IO_COMMON_H_

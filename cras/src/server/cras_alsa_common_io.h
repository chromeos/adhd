/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CRAS_SRC_SERVER_CRAS_ALSA_COMMON_IO_H_
#define CRAS_SRC_SERVER_CRAS_ALSA_COMMON_IO_H_

#include <stdbool.h>
#include <sys/time.h>

#include "cras/src/common/cras_alsa_card_info.h"
#include "cras/src/server/cras_alsa_ucm.h"
#include "cras/src/server/cras_iodev.h"

#ifdef __cplusplus
extern "C" {
#endif

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

// maximum 25 step, volume change 4% once a time
#define NUMBER_OF_VOLUME_STEPS_MAX 25

// minimum 10 step, volume change 10% once a time
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

struct alsa_common_io {
  // The cras_iodev structure "base class".
  struct cras_iodev base;
  // The PCM name passed to snd_pcm_open() (e.g. "hw:0,0").
  char* pcm_name;
  // value from snd_pcm_info_get_name
  char* dev_name;
  // value from snd_pcm_info_get_id
  char* dev_id;
  // ALSA index of device, Y in "hw:X:Y".
  uint32_t device_index;
  // The index we will give to the next ionode. Each ionode
  // have a unique index within the iodev.
  uint32_t next_ionode_index;
  // the type of the card this iodev belongs.
  enum CRAS_ALSA_CARD_TYPE card_type;
  // true if this is the first iodev on the card.
  int is_first;
  // Handle to the opened ALSA device.
  snd_pcm_t* handle;
  // Number of times we have run out of data badly.
  // Unlike num_underruns which records for the duration
  // where device is opened, num_severe_underruns records
  // since device is created. When severe underrun occurs
  // a possible action is to close/open device.
  unsigned int num_severe_underruns;
  // Playback or capture type.
  snd_pcm_stream_t alsa_stream;
  // Alsa mixer used to control volume and mute of the device.
  struct cras_alsa_mixer* mixer;
  // Card config for this alsa device.
  const struct cras_card_config* config;
  // List of alsa jack controls for this device.
  struct cras_alsa_jack_list* jack_list;
  // CRAS use case manager, if configuration is found.
  struct cras_use_case_mgr* ucm;
  // offset returned from mmap_begin.
  snd_pcm_uframes_t mmap_offset;
  // Descriptor used to block until data is ready.
  int poll_fd;
  // If non-zero, the value to apply to the dma_period.
  unsigned int dma_period_set_microsecs;
  // true if device is playing zeros in the buffer without
  // user filling meaningful data. The device buffer is filled
  // with zeros. In this state, appl_ptr remains the same
  // while hw_ptr keeps running ahead.
  int free_running;
  // The number of zeros filled for draining.
  unsigned int filled_zeros_for_draining;
  // The threshold for severe underrun.
  snd_pcm_uframes_t severe_underrun_frames;
  // Default volume curve that converts from an index
  // to dBFS.
  struct cras_volume_curve* default_volume_curve;
  int hwparams_set;
  // true if this iodev has dependent
  int has_dependent_dev;
  // Device vendor id.
  size_t vendor_id;
  // Device product id
  size_t product_id;
  // Last obtained hardware timestamp.
  struct timespec hardware_timestamp;
  // Pointer to mmap buffer. It's mmap-ed in get_buffer() and
  // committed in put_buffer().
  uint8_t* mmap_buf;
  // Pointer to sample buffer. It's malloc in configure_dev() and
  // free in close_dev().
  uint8_t* sample_buf;
};

struct cras_ionode* first_plugged_node(struct cras_iodev* iodev);

// Enable or disable noise cancellation for the active node if supported.
//
// Returns nonzero on unrecoverable failures.
int cras_alsa_common_configure_noise_cancellation(
    struct cras_iodev* iodev,
    struct cras_use_case_mgr* ucm);

// Get the provider for noise cancellation on the node.
enum CRAS_NC_PROVIDER cras_alsa_common_get_nc_providers(
    struct cras_use_case_mgr* ucm,
    const char* node_name);

int cras_alsa_common_set_hwparams(struct cras_iodev* iodev, int period_wakeup);
int cras_alsa_common_frames_queued(const struct cras_iodev* iodev,
                                   struct timespec* tstamp);
int cras_alsa_common_set_active_node(struct cras_iodev* iodev,
                                     struct cras_ionode* ionode);
int cras_alsa_common_delay_frames(const struct cras_iodev* iodev);
int cras_alsa_common_close_dev(const struct cras_iodev* iodev);
int cras_alsa_common_open_dev(struct cras_iodev* iodev, const char* pcm_name);
int cras_alsa_common_get_htimestamp(const struct cras_iodev* iodev,
                                    struct timespec* ts);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_ALSA_COMMON_IO_H_

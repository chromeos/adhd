/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras_alsa_usb_io.h"

#include <alsa/asoundlib.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>

#include "cras/src/server/audio_thread.h"
#include "cras/src/server/cras_alsa_helpers.h"
#include "cras/src/server/cras_alsa_io_common.h"
#include "cras/src/server/cras_alsa_io_ops.h"
#include "cras/src/server/cras_alsa_jack.h"
#include "cras/src/server/cras_alsa_mixer.h"
#include "cras/src/server/cras_alsa_ucm.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_hotword_handler.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_ramp.h"
#include "cras/src/server/cras_rclient.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_utf8.h"
#include "cras/src/server/cras_volume_curve.h"
#include "cras/src/server/dev_stream.h"
#include "cras/src/server/softvol_curve.h"
#include "cras_config.h"
#include "cras_iodev_info.h"
#include "cras_messages.h"
#include "cras_shm.h"
#include "cras_types.h"
#include "cras_utf8.h"
#include "cras_util.h"
#include "third_party/strlcpy/strlcpy.h"
#include "third_party/superfasthash/sfh.h"
#include "third_party/utlist/utlist.h"
/*
 * This extends cras_ionode to include alsa-specific information.
 */
struct alsa_usb_output_node {
  struct cras_ionode base;
  // From cras_alsa_mixer.
  struct mixer_control* mixer_output;
  // PCM name for snd_pcm_open.
  const char* pcm_name;
  // Volume curve for this node.
  struct cras_volume_curve* volume_curve;
  // The jack associated with the node.
  const struct cras_alsa_jack* jack;
};

struct alsa_usb_input_node {
  struct cras_ionode base;
  struct mixer_control* mixer_input;
  const char* pcm_name;
  const struct cras_alsa_jack* jack;
  int8_t* channel_layout;
};

/*
 * Child of cras_iodev, alsa_usb_io handles ALSA interaction for sound devices.
 */
struct alsa_usb_io {
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
  // true if this device and it's nodes were fully specified.
  // That is, don't automatically create nodes for it.
  int fully_specified;
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
};

static void usb_init_device_settings(struct alsa_usb_io* aio);

static int usb_alsa_iodev_set_active_node(struct cras_iodev* iodev,
                                          struct cras_ionode* ionode,
                                          unsigned dev_enabled);

static int usb_get_fixed_rate(struct alsa_usb_io* aio);

static int usb_update_supported_formats(struct cras_iodev* iodev);

static int usb_set_hwparams(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  int rc;

  // Only need to set hardware params once.
  if (aio->hwparams_set) {
    return 0;
  }

  /* Sets frame rate and channel count to alsa device before
   * we test channel mapping. */
  // USB is not a wake on voice device, period_wakeups should be 0
  rc = cras_alsa_set_hwparams(aio->handle, iodev->format, &iodev->buffer_size,
                              0, aio->dma_period_set_microsecs);
  if (rc < 0) {
    return rc;
  }

  aio->hwparams_set = 1;
  return 0;
}

/*
 * iodev callbacks.
 */

static int usb_frames_queued(const struct cras_iodev* iodev,
                             struct timespec* tstamp) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  int rc;
  snd_pcm_uframes_t frames;

  rc = cras_alsa_get_avail_frames(aio->handle, aio->base.buffer_size,
                                  aio->severe_underrun_frames, iodev->info.name,
                                  &frames, tstamp);
  if (rc < 0) {
    if (rc == -EPIPE) {
      aio->num_severe_underruns++;
    }
    return rc;
  }
  clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
  if (iodev->direction == CRAS_STREAM_INPUT) {
    return (int)frames;
  }

  // For output, return number of frames that are used.
  return iodev->buffer_size - frames;
}

static int usb_delay_frames(const struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  snd_pcm_sframes_t delay;
  int rc;

  rc = cras_alsa_get_delay_frames(aio->handle, iodev->buffer_size, &delay);
  if (rc < 0) {
    return rc;
  }

  return (int)delay;
}

static int usb_close_dev(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;

  // Removes audio thread callback from main thread.
  if (aio->poll_fd >= 0) {
    audio_thread_rm_callback_sync(cras_iodev_list_get_audio_thread(),
                                  aio->poll_fd);
  }
  if (!aio->handle) {
    return 0;
  }
  cras_alsa_pcm_close(aio->handle);
  aio->handle = NULL;
  aio->free_running = 0;
  aio->filled_zeros_for_draining = 0;
  aio->hwparams_set = 0;
  cras_iodev_free_format(&aio->base);
  cras_iodev_free_audio_area(&aio->base);
  return 0;
}

static int usb_open_dev(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  snd_pcm_t* handle;
  int rc;
  const char* pcm_name = NULL;

  if (aio->base.direction == CRAS_STREAM_OUTPUT) {
    struct alsa_usb_output_node* aout =
        (struct alsa_usb_output_node*)aio->base.active_node;
    pcm_name = aout->pcm_name;
  } else {
    struct alsa_usb_input_node* ain =
        (struct alsa_usb_input_node*)aio->base.active_node;
    pcm_name = ain->pcm_name;
  }

  // For legacy UCM path which doesn't have PlaybackPCM or CapturePCM.
  if (pcm_name == NULL) {
    pcm_name = aio->pcm_name;
  }

  rc = cras_alsa_pcm_open(&handle, pcm_name, aio->alsa_stream);
  if (rc < 0) {
    return rc;
  }

  aio->handle = handle;

  rc = cras_alsa_common_configure_noise_cancellation(iodev, aio->ucm);
  if (rc) {
    return rc;
  }

  return 0;
}

static int usb_configure_dev(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  int rc;

  /* This is called after the first stream added so configure for it.
   * format must be set before opening the device.
   */
  if (iodev->format == NULL) {
    return -EINVAL;
  }
  aio->free_running = 0;
  aio->filled_zeros_for_draining = 0;
  aio->severe_underrun_frames =
      SEVERE_UNDERRUN_MS * iodev->format->frame_rate / 1000;

  cras_iodev_init_audio_area(iodev, iodev->format->num_channels);

  syslog(LOG_DEBUG, "Configure alsa device %s rate %zuHz, %zu channels",
         aio->pcm_name, iodev->format->frame_rate, iodev->format->num_channels);

  rc = usb_set_hwparams(iodev);
  if (rc < 0) {
    return rc;
  }

  // Set channel map to device
  rc = cras_alsa_set_channel_map(aio->handle, iodev->format);
  if (rc < 0) {
    return rc;
  }

  // Configure software params.
  rc = cras_alsa_set_swparams(aio->handle);
  if (rc < 0) {
    return rc;
  }

  // Initialize device settings.
  usb_init_device_settings(aio);

  aio->poll_fd = -1;

  // Capture starts right away, playback will wait for samples.
  if (aio->alsa_stream == SND_PCM_STREAM_CAPTURE) {
    rc = cras_alsa_pcm_start(aio->handle);
    if (rc < 0) {
      syslog(LOG_ERR, "PCM %s Failed to start, ret: %s\n", aio->pcm_name,
             snd_strerror(rc));
    }
  }

  return 0;
}

/*
 * Check if ALSA device is opened by checking if handle is valid.
 * Note that to fully open a cras_iodev, ALSA device is opened first, then there
 * are some device init settings to be done in usb_init_device_settings.
 * Therefore, when setting volume/mute/gain in usb_init_device_settings,
 * cras_iodev is not in CRAS_IODEV_STATE_OPEN yet. We need to check if handle
 * is valid when setting those properties, instead of checking
 * cras_iodev_is_open.
 */
static int usb_has_handle(const struct alsa_usb_io* aio) {
  return !!aio->handle;
}

static int usb_start(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  snd_pcm_t* handle = aio->handle;
  int rc;

  if (snd_pcm_state(handle) == SND_PCM_STATE_RUNNING) {
    return 0;
  }

  if (snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
    rc = cras_alsa_attempt_resume(handle);
    if (rc < 0) {
      syslog(LOG_ERR, "Resume error: %s", snd_strerror(rc));
      return rc;
    }
    cras_iodev_reset_rate_estimator(iodev);
  } else {
    rc = cras_alsa_pcm_start(handle);
    if (rc < 0) {
      syslog(LOG_ERR, "Start error: %s", snd_strerror(rc));
      return rc;
    }
  }

  return 0;
}

static int usb_get_buffer(struct cras_iodev* iodev,
                          struct cras_audio_area** area,
                          unsigned* frames) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  snd_pcm_uframes_t nframes = *frames;
  uint8_t* dst = NULL;
  size_t format_bytes;
  int rc;

  aio->mmap_offset = 0;
  format_bytes = cras_get_format_bytes(iodev->format);

  rc = cras_alsa_mmap_begin(aio->handle, format_bytes, &dst, &aio->mmap_offset,
                            &nframes);

  iodev->area->frames = nframes;
  cras_audio_area_config_buf_pointers(iodev->area, iodev->format, dst);

  *area = iodev->area;
  *frames = nframes;

  return rc;
}

static int usb_put_buffer(struct cras_iodev* iodev, unsigned nwritten) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;

  return cras_alsa_mmap_commit(aio->handle, aio->mmap_offset, nwritten);
}

static int usb_flush_buffer(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  snd_pcm_uframes_t nframes;

  if (iodev->direction == CRAS_STREAM_INPUT) {
    nframes = snd_pcm_avail(aio->handle);
    nframes = snd_pcm_forwardable(aio->handle);
    return snd_pcm_forward(aio->handle, nframes);
  }
  return 0;
}

static void usb_update_active_node(struct cras_iodev* iodev,
                                   unsigned node_idx,
                                   unsigned dev_enabled) {
  struct cras_ionode* n;

  // If a node exists for node_idx, set it as active.
  DL_FOREACH (iodev->nodes, n) {
    if (n->idx == node_idx) {
      usb_alsa_iodev_set_active_node(iodev, n, dev_enabled);
      return;
    }
  }

  usb_alsa_iodev_set_active_node(iodev, first_plugged_node(iodev), dev_enabled);
}

static int usb_update_channel_layout(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  int err = 0;

  /* If the capture channel map is specified in UCM, prefer it over
   * what ALSA provides. */
  if (aio->ucm && (iodev->direction == CRAS_STREAM_INPUT)) {
    struct alsa_usb_input_node* input =
        (struct alsa_usb_input_node*)iodev->active_node;

    if (input->channel_layout) {
      memcpy(iodev->format->channel_layout, input->channel_layout,
             CRAS_CH_MAX * sizeof(*input->channel_layout));
      return 0;
    }
  }

  err = usb_set_hwparams(iodev);
  if (err < 0) {
    return err;
  }

  return cras_alsa_get_channel_map(aio->handle, iodev->format);
}

/*
 * Alsa helper functions.
 */

static struct alsa_usb_output_node* usb_get_active_output(
    const struct alsa_usb_io* aio) {
  return (struct alsa_usb_output_node*)aio->base.active_node;
}

static struct alsa_usb_input_node* usb_get_active_input(
    const struct alsa_usb_io* aio) {
  return (struct alsa_usb_input_node*)aio->base.active_node;
}

/*
 * Gets the curve for the active output node. If the node doesn't have volume
 * curve specified, return the default volume curve of the parent iodev.
 */
static const struct cras_volume_curve* usb_get_curve_for_output_node(
    const struct alsa_usb_io* aio,
    const struct alsa_usb_output_node* node) {
  if (node && node->volume_curve) {
    return node->volume_curve;
  }
  return aio->default_volume_curve;
}

/*
 * Gets the curve for the active output.
 */
static const struct cras_volume_curve* usb_get_curve_for_active_output(
    const struct alsa_usb_io* aio) {
  struct alsa_usb_output_node* node = usb_get_active_output(aio);
  return usb_get_curve_for_output_node(aio, node);
}

/*
 * Informs the system of the volume limits for this device.
 */
static void usb_set_alsa_volume_limits(struct alsa_usb_io* aio) {
  const struct cras_volume_curve* curve;

  // Only set the limits if the dev is active.
  if (!usb_has_handle(aio)) {
    return;
  }

  curve = usb_get_curve_for_active_output(aio);
  cras_system_set_volume_limits(curve->get_dBFS(curve, 1),  // min
                                curve->get_dBFS(curve, CRAS_MAX_SYSTEM_VOLUME));
}

/*
 * Sets the volume of the playback device to the specified level. Receives a
 * volume index from the system settings, ranging from 0 to 100, converts it to
 * dB using the volume curve, and sends the dB value to alsa.
 */
static void usb_set_alsa_volume(struct cras_iodev* iodev) {
  const struct alsa_usb_io* aio = (const struct alsa_usb_io*)iodev;
  const struct cras_volume_curve* curve;
  size_t volume;
  struct alsa_usb_output_node* aout;

  assert(aio);
  if (aio->mixer == NULL) {
    return;
  }

  volume = cras_system_get_volume();
  curve = usb_get_curve_for_active_output(aio);
  if (curve == NULL) {
    return;
  }
  aout = usb_get_active_output(aio);
  if (aout) {
    volume = cras_iodev_adjust_node_volume(&aout->base, volume);
  }

  /* Samples get scaled for devices using software volume, set alsa
   * volume to 100. */
  if (cras_iodev_software_volume_needed(iodev)) {
    volume = 100;
  }

  cras_alsa_mixer_set_dBFS(aio->mixer, curve->get_dBFS(curve, volume),
                           aout ? aout->mixer_output : NULL);
}

/*
 * Sets the alsa mute control for this iodev.
 */
static void usb_set_alsa_mute(struct cras_iodev* iodev) {
  const struct alsa_usb_io* aio = (const struct alsa_usb_io*)iodev;
  struct alsa_usb_output_node* aout;

  if (!usb_has_handle(aio)) {
    return;
  }

  aout = usb_get_active_output(aio);
  cras_alsa_mixer_set_mute(aio->mixer, cras_system_get_mute(),
                           aout ? aout->mixer_output : NULL);
}

/*
 * Sets the capture gain to the current system input gain level, given in dBFS.
 * Set mute based on the system mute state.  This gain can be positive or
 * negative and might be adjusted often if an app is running an AGC.
 */
static void usb_set_alsa_capture_gain(struct cras_iodev* iodev) {
  const struct alsa_usb_io* aio = (const struct alsa_usb_io*)iodev;
  struct alsa_usb_input_node* ain;
  long min_capture_gain, max_capture_gain, gain;
  assert(aio);
  if (aio->mixer == NULL) {
    return;
  }

  // Only set the volume if the dev is active.
  if (!usb_has_handle(aio)) {
    return;
  }

  ain = usb_get_active_input(aio);

  // For USB device without UCM config, not change a gain control.
  if (!aio->ucm) {
    return;
  }

  // Set hardware gain to 0dB if software gain is needed.
  if (cras_iodev_software_volume_needed(iodev)) {
    gain = 0;
  } else {
    min_capture_gain = cras_alsa_mixer_get_minimum_capture_gain(
        aio->mixer, ain ? ain->mixer_input : NULL);
    max_capture_gain = cras_alsa_mixer_get_maximum_capture_gain(
        aio->mixer, ain ? ain->mixer_input : NULL);
    gain = MAX(iodev->active_node->capture_gain, min_capture_gain);
    gain = MIN(gain, max_capture_gain);
  }

  cras_alsa_mixer_set_capture_dBFS(aio->mixer, gain,
                                   ain ? ain->mixer_input : NULL);
}

/*
 * Swaps the left and right channels of the given node.
 */
static int usb_set_alsa_node_swapped(struct cras_iodev* iodev,
                                     struct cras_ionode* node,
                                     int enable) {
  const struct alsa_usb_io* aio = (const struct alsa_usb_io*)iodev;
  assert(aio);
  return ucm_enable_swap_mode(aio->ucm, node->ucm_name, enable);
}

/*
 * Initializes the device settings according to system volume, mute, gain
 * settings.
 * Updates system capture gain limits based on current active device/node.
 */
static void usb_init_device_settings(struct alsa_usb_io* aio) {
  /* Register for volume/mute callback and set initial volume/mute for
   * the device. */
  if (aio->base.direction == CRAS_STREAM_OUTPUT) {
    usb_set_alsa_volume_limits(aio);
    usb_set_alsa_volume(&aio->base);
    usb_set_alsa_mute(&aio->base);
  } else {
    usb_set_alsa_capture_gain(&aio->base);
  }
}

/*
 * Functions run in the main server context.
 */

/*
 * Frees resources used by the alsa iodev.
 * Args:
 *    iodev - the iodev to free the resources from.
 */
static void usb_free_alsa_iodev_resources(struct alsa_usb_io* aio) {
  struct cras_ionode* node;
  struct alsa_usb_output_node* aout;
  struct alsa_usb_input_node* ain;

  free(aio->base.supported_rates);
  free(aio->base.supported_channel_counts);
  free(aio->base.supported_formats);

  DL_FOREACH (aio->base.nodes, node) {
    if (aio->base.direction == CRAS_STREAM_OUTPUT) {
      aout = (struct alsa_usb_output_node*)node;
      cras_volume_curve_destroy(aout->volume_curve);
      free((void*)aout->pcm_name);
    } else {
      ain = (struct alsa_usb_input_node*)node;
      free((void*)ain->pcm_name);
    }
    cras_iodev_rm_node(&aio->base, node);
    free(node->softvol_scalers);
    free((void*)node->dsp_name);
    free(node);
  }

  cras_iodev_free_resources(&aio->base);
  free(aio->pcm_name);
  if (aio->dev_id) {
    free(aio->dev_id);
  }
  if (aio->dev_name) {
    free(aio->dev_name);
  }
}

/*
 * Drop the node name and replace it with node type.
 */
static void usb_drop_node_name(struct cras_ionode* node) {
  strlcpy(node->name, USB, sizeof(node->name));
}

/*
 * Sets the initial plugged state and type of a node based on its
 * name. Chrome will assign priority to nodes base on node type.
 */
static void usb_set_node_initial_state(struct cras_ionode* node) {
  node->volume = 100;

  /* Regardless of the node name of a USB headset (it can be "Speaker"),
   * set it's type to usb.
   */
  node->type = CRAS_NODE_TYPE_USB;
  node->position = NODE_POSITION_EXTERNAL;

  if (!is_utf8_string(node->name)) {
    usb_drop_node_name(node);
  }
}

static int usb_get_ucm_flag_integer(struct alsa_usb_io* aio,
                                    const char* flag_name,
                                    int* result) {
  char* value;
  int i;

  if (!aio->ucm) {
    return -ENOENT;
  }

  value = ucm_get_flag(aio->ucm, flag_name);
  if (!value) {
    return -EINVAL;
  }

  i = atoi(value);
  free(value);
  *result = i;
  return 0;
}

static int usb_auto_unplug_input_node(struct alsa_usb_io* aio) {
  int result;
  if (usb_get_ucm_flag_integer(aio, "AutoUnplugInputNode", &result)) {
    return 0;
  }
  return result;
}

static int usb_auto_unplug_output_node(struct alsa_usb_io* aio) {
  int result;
  if (usb_get_ucm_flag_integer(aio, "AutoUnplugOutputNode", &result)) {
    return 0;
  }
  return result;
}

static int usb_no_create_default_input_node(struct alsa_usb_io* aio) {
  int result;
  if (usb_get_ucm_flag_integer(aio, "NoCreateDefaultInputNode", &result)) {
    return 0;
  }
  return result;
}

static int usb_no_create_default_output_node(struct alsa_usb_io* aio) {
  int result;
  if (usb_get_ucm_flag_integer(aio, "NoCreateDefaultOutputNode", &result)) {
    return 0;
  }
  return result;
}

static void usb_set_output_node_software_volume_needed(
    struct alsa_usb_output_node* output,
    struct alsa_usb_io* aio) {
  struct cras_alsa_mixer* mixer = aio->mixer;
  long max, min;
  int32_t number_of_volume_steps;

  cras_alsa_mixer_get_playback_dBFS_range(mixer, output->mixer_output, &max,
                                          &min);
  long volume_range_db = max - min;
  if (volume_range_db < db_to_alsa_db(VOLUME_RANGE_DB_MIN) ||
      volume_range_db > db_to_alsa_db(VOLUME_RANGE_DB_MAX)) {
    output->base.software_volume_needed = 1;
    syslog(LOG_WARNING,
           "%s' output volume range [%ld %ld] is abnormal."
           "Fallback to software volume",
           output->base.name, min, max);
  }
  number_of_volume_steps =
      cras_alsa_mixer_get_playback_step(output->mixer_output);
  if (number_of_volume_steps < NUMBER_OF_VOLUME_STEPS_MIN) {
    output->base.software_volume_needed = 1;
    output->base.number_of_volume_steps = NUMBER_OF_VOLUME_STEPS_DEFAULT;
    syslog(LOG_WARNING,
           "%s output number_of_volume_steps [%" PRId32
           "] is abnormally small."
           "Fallback to software volume and set number_of_volume_steps to %d",
           output->base.name, number_of_volume_steps,
           NUMBER_OF_VOLUME_STEPS_DEFAULT);
  }
  if (output->base.software_volume_needed) {
    syslog(LOG_DEBUG, "Use software volume for node: %s", output->base.name);
  }
}

static void usb_set_input_default_node_gain(struct alsa_usb_input_node* input,
                                            struct alsa_usb_io* aio) {
  long gain;

  input->base.capture_gain = DEFAULT_CAPTURE_GAIN;
  input->base.ui_gain_scaler = 1.0f;

  if (!aio->ucm) {
    return;
  }

  if (ucm_get_default_node_gain(aio->ucm, input->base.ucm_name, &gain) == 0) {
    input->base.capture_gain = gain;
  }
}

static void usb_set_input_node_intrinsic_sensitivity(
    struct alsa_usb_input_node* input,
    struct alsa_usb_io* aio) {
  long sensitivity;
  int rc;

  input->base.intrinsic_sensitivity = 0;

  if (aio->ucm) {
    rc = ucm_get_intrinsic_sensitivity(aio->ucm, input->base.ucm_name,
                                       &sensitivity);
    if (rc) {
      return;
    }
  } else {
    /*
     * For USB devices without UCM config, trust the default capture gain.
     * Set sensitivity to the default dbfs so the capture gain is 0.
     */
    sensitivity = DEFAULT_CAPTURE_VOLUME_DBFS;
  }
  input->base.intrinsic_sensitivity = sensitivity;
  input->base.capture_gain = DEFAULT_CAPTURE_VOLUME_DBFS - sensitivity;
  syslog(LOG_INFO,
         "Use software gain %ld for %s because IntrinsicSensitivity %ld is"
         " specified in UCM",
         input->base.capture_gain, input->base.name, sensitivity);
}

static void usb_check_auto_unplug_output_node(struct alsa_usb_io* aio,
                                              struct cras_ionode* node,
                                              int plugged) {
  struct cras_ionode* tmp;

  if (!usb_auto_unplug_output_node(aio)) {
    return;
  }

  // Auto unplug internal speaker if any output node has been created
  if (!strcmp(node->name, INTERNAL_SPEAKER) && plugged) {
    DL_FOREACH (aio->base.nodes, tmp) {
      if (tmp->plugged && (tmp != node)) {
        cras_iodev_set_node_plugged(node, 0);
      }
    }
  } else {
    DL_FOREACH (aio->base.nodes, tmp) {
      if (!strcmp(tmp->name, INTERNAL_SPEAKER)) {
        cras_iodev_set_node_plugged(tmp, !plugged);
      }
    }
  }
}

/*
 * Callback for listing mixer outputs. The mixer will call this once for each
 * output associated with this device. Most commonly this is used to tell the
 * device it has Headphones and Speakers.
 */
static struct alsa_usb_output_node* usb_new_output(
    struct alsa_usb_io* aio,
    struct mixer_control* cras_control,
    const char* name) {
  struct alsa_usb_output_node* output;
  struct cras_volume_curve* curve;
  long max_volume, min_volume;
  int32_t number_of_volume_steps;
  unsigned int disable_software_volume = 0;

  syslog(LOG_DEBUG, "New output node for '%s'", name);
  if (aio == NULL) {
    syslog(LOG_ERR, "Invalid aio when listing outputs.");
    return NULL;
  }
  output = (struct alsa_usb_output_node*)calloc(1, sizeof(*output));
  if (output == NULL) {
    syslog(LOG_ERR, "Out of memory when listing outputs.");
    return NULL;
  }
  output->base.dev = &aio->base;
  output->base.idx = aio->next_ionode_index++;
  output->base.stable_id =
      SuperFastHash(name, strlen(name), aio->base.info.stable_id);

  output->base.number_of_volume_steps = NUMBER_OF_VOLUME_STEPS_DEFAULT;
  if (aio->ucm) {
    output->base.dsp_name = ucm_get_dsp_name_for_dev(aio->ucm, name);
    disable_software_volume = ucm_get_disable_software_volume(aio->ucm);
  }
  output->mixer_output = cras_control;
  // Volume curve.
  curve = cras_card_config_get_volume_curve_for_control(
      aio->config,
      name ? name : cras_alsa_mixer_get_control_name(cras_control));
  if (!curve) {
    cras_alsa_mixer_get_playback_dBFS_range(aio->mixer, cras_control,
                                            &max_volume, &min_volume);
    syslog(LOG_DEBUG, "%s's output volume range: [%ld %ld]", name, min_volume,
           max_volume);
    long long volume_range_db = max_volume - min_volume;
    /* if we specified to disable sw volume or your headset volume
     * range is with a reasonable range, create volume curve. */
    if (disable_software_volume ||
        (volume_range_db >= db_to_alsa_db(VOLUME_RANGE_DB_MIN) &&
         volume_range_db <= db_to_alsa_db(VOLUME_RANGE_DB_MAX))) {
      curve = cras_volume_curve_create_simple_step(0, volume_range_db);
    }
    number_of_volume_steps = cras_alsa_mixer_get_playback_step(cras_control);
    output->base.number_of_volume_steps =
        MIN(number_of_volume_steps, NUMBER_OF_VOLUME_STEPS_MAX);
  }
  output->volume_curve = curve;

  strncpy(output->base.name, name, sizeof(output->base.name) - 1);
  strncpy(output->base.ucm_name, name, sizeof(output->base.ucm_name) - 1);
  usb_set_node_initial_state(&output->base);
  if (disable_software_volume) {
    syslog(LOG_DEBUG, "Disable software volume for %s from ucm.",
           output->base.name);
  } else {
    usb_set_output_node_software_volume_needed(output, aio);
  }
  cras_iodev_add_node(&aio->base, &output->base);

  usb_check_auto_unplug_output_node(aio, &output->base, output->base.plugged);
  return output;
}

static void usb_new_output_by_mixer_control(struct mixer_control* cras_output,
                                            void* callback_arg) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)callback_arg;
  char node_name[CRAS_IODEV_NAME_BUFFER_SIZE];
  const char* ctl_name;
  ctl_name = cras_alsa_mixer_get_control_name(cras_output);
  if (!ctl_name) {
    return;
  }
  if (snprintf(node_name, sizeof(node_name), "%s: %s", aio->base.info.name,
               ctl_name) > 0) {
    usb_new_output(aio, cras_output, node_name);
  }
}

static void usb_check_auto_unplug_input_node(struct alsa_usb_io* aio,
                                             struct cras_ionode* node,
                                             int plugged) {
  struct cras_ionode* tmp;
  if (!usb_auto_unplug_input_node(aio)) {
    return;
  }

  /* Auto unplug internal mic if any input node has already
   * been created */
  if (!strcmp(node->name, INTERNAL_MICROPHONE) && plugged) {
    DL_FOREACH (aio->base.nodes, tmp) {
      if (tmp->plugged && (tmp != node)) {
        cras_iodev_set_node_plugged(node, 0);
      }
    }
  } else {
    DL_FOREACH (aio->base.nodes, tmp) {
      if (!strcmp(tmp->name, INTERNAL_MICROPHONE)) {
        cras_iodev_set_node_plugged(tmp, !plugged);
      }
    }
  }
}

static struct alsa_usb_input_node* usb_new_input(
    struct alsa_usb_io* aio,
    struct mixer_control* cras_input,
    const char* name) {
  struct cras_iodev* iodev = &aio->base;
  struct alsa_usb_input_node* input;
  int err;

  input = (struct alsa_usb_input_node*)calloc(1, sizeof(*input));
  if (input == NULL) {
    syslog(LOG_ERR, "Out of memory when listing inputs.");
    return NULL;
  }
  input->base.dev = &aio->base;
  input->base.idx = aio->next_ionode_index++;
  input->base.stable_id =
      SuperFastHash(name, strlen(name), aio->base.info.stable_id);
  input->mixer_input = cras_input;
  strncpy(input->base.name, name, sizeof(input->base.name) - 1);
  strncpy(input->base.ucm_name, name, sizeof(input->base.ucm_name) - 1);
  usb_set_node_initial_state(&input->base);
  usb_set_input_default_node_gain(input, aio);
  usb_set_input_node_intrinsic_sensitivity(input, aio);

  if (aio->ucm) {
    // Check if channel map is specified in UCM.
    input->channel_layout =
        (int8_t*)malloc(CRAS_CH_MAX * sizeof(*input->channel_layout));
    err = ucm_get_capture_chmap_for_dev(aio->ucm, name, input->channel_layout);
    if (err) {
      free(input->channel_layout);
      input->channel_layout = 0;
    }
    if (ucm_get_preempt_hotword(aio->ucm, name)) {
      iodev->pre_open_iodev_hook = cras_iodev_list_suspend_hotword_streams;
      iodev->post_close_iodev_hook = cras_iodev_list_resume_hotword_stream;
    }

    input->base.dsp_name = ucm_get_dsp_name_for_dev(aio->ucm, name);
  }

  // Set NC provider.
  input->base.nc_provider = cras_alsa_common_get_nc_provider(aio->ucm, name);

  cras_iodev_add_node(&aio->base, &input->base);
  usb_check_auto_unplug_input_node(aio, &input->base, input->base.plugged);
  return input;
}

static void usb_new_input_by_mixer_control(struct mixer_control* cras_input,
                                           void* callback_arg) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)callback_arg;
  char node_name[CRAS_IODEV_NAME_BUFFER_SIZE];
  const char* ctl_name = cras_alsa_mixer_get_control_name(cras_input);
  int ret = snprintf(node_name, sizeof(node_name), "%s: %s",
                     aio->base.info.name, ctl_name);
  // Truncation is OK, but add a check to make the compiler happy.
  if (ret == sizeof(node_name)) {
    node_name[sizeof(node_name) - 1] = '\0';
  }
  usb_new_input(aio, cras_input, node_name);
}

/*
 * Finds the output node associated with the jack. Returns NULL if not found.
 */
static struct alsa_usb_output_node* usb_get_output_node_from_jack(
    struct alsa_usb_io* aio,
    const struct cras_alsa_jack* jack) {
  struct mixer_control* mixer_output;
  struct cras_ionode* node = NULL;
  struct alsa_usb_output_node* aout = NULL;

  // Search by jack first.
  DL_SEARCH_SCALAR_WITH_CAST(aio->base.nodes, node, aout, jack, jack);
  if (aout) {
    return aout;
  }

  // Search by mixer control next.
  mixer_output = cras_alsa_jack_get_mixer_output(jack);
  if (mixer_output == NULL) {
    return NULL;
  }

  DL_SEARCH_SCALAR_WITH_CAST(aio->base.nodes, node, aout, mixer_output,
                             mixer_output);
  return aout;
}

static struct alsa_usb_input_node* usb_get_input_node_from_jack(
    struct alsa_usb_io* aio,
    const struct cras_alsa_jack* jack) {
  struct mixer_control* mixer_input;
  struct cras_ionode* node = NULL;
  struct alsa_usb_input_node* ain = NULL;

  mixer_input = cras_alsa_jack_get_mixer_input(jack);
  if (mixer_input == NULL) {
    DL_SEARCH_SCALAR_WITH_CAST(aio->base.nodes, node, ain, jack, jack);
    return ain;
  }

  DL_SEARCH_SCALAR_WITH_CAST(aio->base.nodes, node, ain, mixer_input,
                             mixer_input);
  return ain;
}

static const struct cras_alsa_jack* usb_get_jack_from_node(
    struct cras_ionode* node) {
  const struct cras_alsa_jack* jack = NULL;

  if (node == NULL) {
    return NULL;
  }

  if (node->dev->direction == CRAS_STREAM_OUTPUT) {
    jack = ((struct alsa_usb_output_node*)node)->jack;
  } else if (node->dev->direction == CRAS_STREAM_INPUT) {
    jack = ((struct alsa_usb_input_node*)node)->jack;
  }

  return jack;
}

/*
 * Creates volume curve for the node associated with given jack.
 */
static struct cras_volume_curve* usb_create_volume_curve_for_jack(
    const struct cras_card_config* config,
    const struct cras_alsa_jack* jack) {
  struct cras_volume_curve* curve;
  const char* name;

  // Use jack's UCM device name as key to get volume curve.
  name = cras_alsa_jack_get_ucm_device(jack);
  curve = cras_card_config_get_volume_curve_for_control(config, name);
  if (curve) {
    return curve;
  }

  // Use alsa jack's name as key to get volume curve.
  name = cras_alsa_jack_get_name(jack);
  curve = cras_card_config_get_volume_curve_for_control(config, name);
  if (curve) {
    return curve;
  }

  return NULL;
}

/*
 * Updates max_supported_channels value into cras_iodev_info.
 * Note that supported_rates, supported_channel_counts, and supported_formats of
 * iodev will be updated to the latest values after calling.
 */
static void usb_update_max_supported_channels(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  unsigned int max_channels = 0;
  size_t i;
  bool active_node_predicted = false;
  int rc;

  /*
   * max_supported_channels might be wrong in dependent PCM cases. Always
   * return 2 for such cases.
   */
  if (aio->has_dependent_dev) {
    max_channels = 2;
    goto update_info;
  }

  if (aio->handle) {
    syslog(LOG_ERR,
           "usb_update_max_supported_channels should not be called "
           "while device is opened.");
    return;
  }

  /*
   * In the case of updating max_supported_channels on changing jack
   * plugging status of devices, the active node may not be determined
   * yet. Use the first node as the active node for obtaining the value of
   * max_supported_channels.
   */
  if (!iodev->active_node) {
    if (!iodev->nodes) {
      goto update_info;
    }
    iodev->active_node = iodev->nodes;
    syslog(LOG_DEBUG, "Predict ionode %s as active node temporarily.",
           iodev->active_node->name);
    active_node_predicted = true;
  }

  rc = usb_open_dev(iodev);
  if (active_node_predicted) {
    iodev->active_node = NULL;  // Reset the predicted active_node.
  }
  if (rc) {
    goto update_info;
  }

  rc = usb_update_supported_formats(iodev);
  if (rc) {
    goto close_iodev;
  }

  for (i = 0; iodev->supported_channel_counts[i] != 0; i++) {
    if (iodev->supported_channel_counts[i] > max_channels) {
      max_channels = iodev->supported_channel_counts[i];
    }
  }

close_iodev:
  usb_close_dev(iodev);

update_info:
  iodev->info.max_supported_channels = max_channels;
}

/*
 * Callback that is called when an output jack is plugged or unplugged.
 */
static void usb_jack_output_plug_event(const struct cras_alsa_jack* jack,
                                       int plugged,
                                       void* arg) {
  struct alsa_usb_io* aio;
  struct alsa_usb_output_node* node;
  const char* jack_name;

  if (arg == NULL) {
    return;
  }

  aio = (struct alsa_usb_io*)arg;
  node = usb_get_output_node_from_jack(aio, jack);
  jack_name = cras_alsa_jack_get_name(jack);
  if (!jack_name || !strcmp(jack_name, "Speaker Phantom Jack")) {
    jack_name = INTERNAL_SPEAKER;
  }

  // If there isn't a node for this jack, create one.
  if (node == NULL) {
    if (aio->fully_specified) {
      // When fully specified, can't have new nodes.
      syslog(LOG_ERR, "No matching output node for jack %s!", jack_name);
      return;
    }
    node = usb_new_output(aio, NULL, jack_name);
    if (node == NULL) {
      return;
    }

    cras_alsa_jack_update_node_type(jack, &(node->base.type));
  }

  if (!node->jack) {
    if (aio->fully_specified) {
      syslog(LOG_ERR,
             "Jack '%s' was found to match output node '%s'."
             " Please fix your UCM configuration to match.",
             jack_name, node->base.ucm_name);
    }

    // If we already have the node, associate with the jack.
    node->jack = jack;
    if (node->volume_curve == NULL) {
      node->volume_curve = usb_create_volume_curve_for_jack(aio->config, jack);
    }
  }

  syslog(LOG_DEBUG, "%s plugged: %d, %s", jack_name, plugged,
         cras_alsa_mixer_get_control_name(node->mixer_output));

  cras_alsa_jack_update_monitor_name(jack, node->base.name,
                                     sizeof(node->base.name));
  // The name got from jack might be an invalid UTF8 string.
  if (!is_utf8_string(node->base.name)) {
    usb_drop_node_name(&node->base);
  }

  cras_iodev_set_node_plugged(&node->base, plugged);

  usb_check_auto_unplug_output_node(aio, &node->base, plugged);
}

/*
 * Callback that is called when an input jack is plugged or unplugged.
 */
static void usb_jack_input_plug_event(const struct cras_alsa_jack* jack,
                                      int plugged,
                                      void* arg) {
  struct alsa_usb_io* aio;
  struct alsa_usb_input_node* node;
  struct mixer_control* cras_input;
  const char* jack_name;

  if (arg == NULL) {
    return;
  }
  aio = (struct alsa_usb_io*)arg;
  node = usb_get_input_node_from_jack(aio, jack);
  jack_name = cras_alsa_jack_get_name(jack);

  // If there isn't a node for this jack, create one.
  if (node == NULL) {
    if (aio->fully_specified) {
      // When fully specified, can't have new nodes.
      syslog(LOG_ERR, "No matching input node for jack %s!", jack_name);
      return;
    }
    cras_input = cras_alsa_jack_get_mixer_input(jack);
    node = usb_new_input(aio, cras_input, jack_name);
    if (node == NULL) {
      return;
    }
  }

  syslog(LOG_DEBUG, "%s plugged: %d, %s", jack_name, plugged,
         cras_alsa_mixer_get_control_name(node->mixer_input));

  // If we already have the node, associate with the jack.
  if (!node->jack) {
    if (aio->fully_specified) {
      syslog(LOG_ERR,
             "Jack '%s' was found to match input node '%s'."
             " Please fix your UCM configuration to match.",
             jack_name, node->base.ucm_name);
    }
    node->jack = jack;
  }

  cras_iodev_set_node_plugged(&node->base, plugged);

  usb_check_auto_unplug_input_node(aio, &node->base, plugged);
}

/*
 * Sets the name of the given iodev, using the name and index of the card
 * combined with the device index and direction.
 */
static void usb_set_iodev_name(struct cras_iodev* dev,
                               const char* card_name,
                               const char* dev_name,
                               size_t card_index,
                               size_t device_index,
                               enum CRAS_ALSA_CARD_TYPE card_type,
                               size_t usb_vid,
                               size_t usb_pid,
                               char* usb_serial_number) {
  snprintf(dev->info.name, sizeof(dev->info.name), "%s: %s:%zu,%zu", card_name,
           dev_name, card_index, device_index);
  dev->info.name[ARRAY_SIZE(dev->info.name) - 1] = '\0';
  syslog(LOG_DEBUG, "Add device name=%s", dev->info.name);

  dev->info.stable_id =
      SuperFastHash(card_name, strlen(card_name), strlen(card_name));
  dev->info.stable_id =
      SuperFastHash(dev_name, strlen(dev_name), dev->info.stable_id);

  dev->info.stable_id = SuperFastHash((const char*)&usb_vid, sizeof(usb_vid),
                                      dev->info.stable_id);
  dev->info.stable_id = SuperFastHash((const char*)&usb_pid, sizeof(usb_pid),
                                      dev->info.stable_id);
  dev->info.stable_id = SuperFastHash(
      usb_serial_number, strlen(usb_serial_number), dev->info.stable_id);
  syslog(LOG_DEBUG, "Stable ID=%08x", dev->info.stable_id);
}

static int usb_get_fixed_rate(struct alsa_usb_io* aio) {
  const char* name;

  if (aio->base.direction == CRAS_STREAM_OUTPUT) {
    struct alsa_usb_output_node* active = usb_get_active_output(aio);
    if (!active) {
      return -ENOENT;
    }
    name = active->base.ucm_name;
  } else {
    struct alsa_usb_input_node* active = usb_get_active_input(aio);
    if (!active) {
      return -ENOENT;
    }
    name = active->base.ucm_name;
  }

  return ucm_get_sample_rate_for_dev(aio->ucm, name, aio->base.direction);
}

static size_t usb_get_fixed_channels(struct alsa_usb_io* aio) {
  const char* name;
  int rc;
  size_t channels;

  if (aio->base.direction == CRAS_STREAM_OUTPUT) {
    struct alsa_usb_output_node* active = usb_get_active_output(aio);
    if (!active) {
      return -ENOENT;
    }
    name = active->base.ucm_name;
  } else {
    struct alsa_usb_input_node* active = usb_get_active_input(aio);
    if (!active) {
      return -ENOENT;
    }
    name = active->base.ucm_name;
  }

  rc = ucm_get_channels_for_dev(aio->ucm, name, aio->base.direction, &channels);
  return (rc) ? 0 : channels;
}

/*
 * Updates the supported sample rates and channel counts.
 */
static int usb_update_supported_formats(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  int err;
  int fixed_rate;
  size_t fixed_channels;

  free(iodev->supported_rates);
  iodev->supported_rates = NULL;
  free(iodev->supported_channel_counts);
  iodev->supported_channel_counts = NULL;
  free(iodev->supported_formats);
  iodev->supported_formats = NULL;

  err = cras_alsa_fill_properties(aio->handle, &iodev->supported_rates,
                                  &iodev->supported_channel_counts,
                                  &iodev->supported_formats);
  if (err) {
    return err;
  }

  if (aio->ucm) {
    // Allow UCM to override supplied rates.
    fixed_rate = usb_get_fixed_rate(aio);
    if (fixed_rate > 0) {
      free(iodev->supported_rates);
      iodev->supported_rates =
          (size_t*)malloc(2 * sizeof(iodev->supported_rates[0]));
      iodev->supported_rates[0] = fixed_rate;
      iodev->supported_rates[1] = 0;
    }

    // Allow UCM to override supported channel counts.
    fixed_channels = usb_get_fixed_channels(aio);
    if (fixed_channels > 0) {
      free(iodev->supported_channel_counts);
      iodev->supported_channel_counts =
          (size_t*)malloc(2 * sizeof(iodev->supported_channel_counts[0]));
      iodev->supported_channel_counts[0] = fixed_channels;
      iodev->supported_channel_counts[1] = 0;
    }
  }
  return 0;
}

/*
 * Builds software volume scalers for output nodes in the device.
 */
static void usb_build_softvol_scalers(struct alsa_usb_io* aio) {
  struct cras_ionode* ionode;

  DL_FOREACH (aio->base.nodes, ionode) {
    struct alsa_usb_output_node* aout;
    const struct cras_volume_curve* curve;

    aout = (struct alsa_usb_output_node*)ionode;
    curve = usb_get_curve_for_output_node(aio, aout);

    ionode->softvol_scalers = softvol_build_from_curve(curve);
  }
}

static void usb_enable_active_ucm(struct alsa_usb_io* aio, int plugged) {
  const struct cras_alsa_jack* jack;
  const char* name;

  if (aio->base.direction == CRAS_STREAM_OUTPUT) {
    struct alsa_usb_output_node* active = usb_get_active_output(aio);
    if (!active) {
      return;
    }
    name = active->base.ucm_name;
    jack = active->jack;
  } else {
    struct alsa_usb_input_node* active = usb_get_active_input(aio);
    if (!active) {
      return;
    }
    name = active->base.ucm_name;
    jack = active->jack;
  }

  if (jack) {
    cras_alsa_jack_enable_ucm(jack, plugged);
  } else if (aio->ucm) {
    ucm_set_enabled(aio->ucm, name, plugged);
  }
}

static int usb_fill_whole_buffer_with_zeros(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  int rc;
  uint8_t* dst = NULL;
  size_t format_bytes;

  // Fill whole buffer with zeros.
  rc = cras_alsa_mmap_get_whole_buffer(aio->handle, &dst);

  if (rc < 0) {
    syslog(LOG_WARNING, "Failed to get whole buffer: %s", snd_strerror(rc));
    return rc;
  }

  format_bytes = cras_get_format_bytes(iodev->format);
  memset(dst, 0, iodev->buffer_size * format_bytes);

  return 0;
}

/*
 * Move appl_ptr to min_buffer_level + min_cb_level frames ahead of hw_ptr
 * when resuming from free run.
 */
static int usb_adjust_appl_ptr_for_leaving_free_run(struct cras_iodev* odev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)odev;
  snd_pcm_uframes_t ahead;

  ahead = odev->min_buffer_level + odev->min_cb_level;
  return cras_alsa_resume_appl_ptr(aio->handle, ahead, NULL);
}

/*
 * Move appl_ptr to min_buffer_level + min_cb_level * 1.5 frames ahead of
 * hw_ptr when adjusting appl_ptr from underrun.
 */
static int usb_adjust_appl_ptr_for_underrun(struct cras_iodev* odev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)odev;
  snd_pcm_uframes_t ahead;
  int actual_appl_ptr_displacement = 0;
  int rc;

  ahead = odev->min_buffer_level + odev->min_cb_level + odev->min_cb_level / 2;
  rc = cras_alsa_resume_appl_ptr(aio->handle, ahead,
                                 &actual_appl_ptr_displacement);
  /* If appl_ptr is actually adjusted, report the glitch.
   * The duration of the glitch is calculated using the number of frames that
   * the appl_ptr is actually adjusted by*/
  if (actual_appl_ptr_displacement > 0) {
    cras_iodev_update_underrun_duration(odev, actual_appl_ptr_displacement);
  }

  return rc;
}

/* This function is for leaving no-stream state but still not in free run yet.
 * The device may have valid samples remaining. We need to adjust appl_ptr to
 * the correct position, which is MAX(min_cb_level + min_buffer_level,
 * valid_sample) */
static int usb_adjust_appl_ptr_samples_remaining(struct cras_iodev* odev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)odev;
  int rc;
  unsigned int real_hw_level, valid_sample, offset;
  struct timespec hw_tstamp;

  /* Get the amount of valid samples which haven't been played yet.
   * The real_hw_level is the real hw_level in device buffer. It doesn't
   * subtract min_buffer_level. */
  valid_sample = 0;
  rc = odev->frames_queued(odev, &hw_tstamp);
  if (rc < 0) {
    return rc;
  }
  real_hw_level = rc;

  /*
   * If underrun happened, handle it. Because usb_alsa_output_underrun function
   * has already called adjust_appl_ptr, we don't need to call it again.
   */
  if (real_hw_level <= odev->min_buffer_level) {
    return cras_iodev_output_underrun(odev, real_hw_level, 0);
  }

  if (real_hw_level > aio->filled_zeros_for_draining) {
    valid_sample = real_hw_level - aio->filled_zeros_for_draining;
  }

  offset = MAX(odev->min_buffer_level + odev->min_cb_level, valid_sample);

  // Fill zeros to make sure there are enough zero samples in device buffer.
  if (offset > real_hw_level) {
    rc = cras_iodev_fill_odev_zeros(odev, offset - real_hw_level, false);
    if (rc) {
      return rc;
    }
  }
  return cras_alsa_resume_appl_ptr(aio->handle, offset, NULL);
}

static int usb_alsa_output_underrun(struct cras_iodev* odev) {
  int rc;

  /* Fill whole buffer with zeros. This avoids samples left in buffer causing
   * noise when device plays them. */
  rc = usb_fill_whole_buffer_with_zeros(odev);
  if (rc) {
    return rc;
  }
  // Adjust appl_ptr to leave underrun.
  return usb_adjust_appl_ptr_for_underrun(odev);
}

static int usb_possibly_enter_free_run(struct cras_iodev* odev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)odev;
  int rc;
  unsigned int real_hw_level, fr_to_write;
  struct timespec hw_tstamp;

  if (aio->free_running) {
    return 0;
  }

  /* Check if all valid samples are played. If all valid samples are played,
   * fill whole buffer with zeros. The real_hw_level is the real hw_level in
   * device buffer. It doesn't subtract min_buffer_level.*/
  rc = odev->frames_queued(odev, &hw_tstamp);
  if (rc < 0) {
    return rc;
  }
  real_hw_level = rc;

  // If underrun happened, handle it and enter free run state.
  if (real_hw_level <= odev->min_buffer_level) {
    rc = cras_iodev_output_underrun(odev, real_hw_level, 0);
    if (rc < 0) {
      return rc;
    }
    aio->free_running = 1;
    return 0;
  }

  if (real_hw_level <= aio->filled_zeros_for_draining || real_hw_level == 0) {
    rc = usb_fill_whole_buffer_with_zeros(odev);
    if (rc < 0) {
      return rc;
    }
    aio->free_running = 1;
    return 0;
  }

  // Fill zeros to drain valid samples.
  fr_to_write = MIN(cras_time_to_frames(&no_stream_fill_zeros_duration,
                                        odev->format->frame_rate),
                    odev->buffer_size - real_hw_level);
  rc = cras_iodev_fill_odev_zeros(odev, fr_to_write, false);
  if (rc) {
    return rc;
  }
  aio->filled_zeros_for_draining += fr_to_write;

  return 0;
}

static int usb_leave_free_run(struct cras_iodev* odev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)odev;
  int rc;

  /* Restart rate estimation because free run internval should not
   * be included. */
  cras_iodev_reset_rate_estimator(odev);

  if (aio->free_running) {
    rc = usb_adjust_appl_ptr_for_leaving_free_run(odev);
  } else {
    rc = usb_adjust_appl_ptr_samples_remaining(odev);
  }
  if (rc) {
    syslog(LOG_WARNING, "device %s failed to leave free run, rc = %d",
           odev->info.name, rc);
    return rc;
  }
  aio->free_running = 0;
  aio->filled_zeros_for_draining = 0;

  return 0;
}

/*
 * Free run state is the optimization of usb_no_stream playback on alsa_usb_io.
 * The whole buffer will be filled with zeros. Device can play these zeros
 * indefinitely. When there is new meaningful sample, appl_ptr should be
 * resumed to some distance ahead of hw_ptr.
 */
static int usb_no_stream(struct cras_iodev* odev, int enable) {
  if (enable) {
    return usb_possibly_enter_free_run(odev);
  } else {
    return usb_leave_free_run(odev);
  }
}

static int usb_is_free_running(const struct cras_iodev* odev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)odev;

  return aio->free_running;
}

static unsigned int usb_get_num_severe_underruns(
    const struct cras_iodev* iodev) {
  const struct alsa_usb_io* aio = (const struct alsa_usb_io*)iodev;
  return aio->num_severe_underruns;
}

static int usb_get_valid_frames(struct cras_iodev* odev,
                                struct timespec* tstamp) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)odev;
  int rc;
  unsigned int real_hw_level;

  /*
   * Get the amount of valid frames which haven't been played yet.
   * The real_hw_level is the real hw_level in device buffer. It doesn't
   * subtract min_buffer_level.
   */
  if (aio->free_running) {
    clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
    return 0;
  }

  rc = odev->frames_queued(odev, tstamp);
  if (rc < 0) {
    return rc;
  }
  real_hw_level = rc;

  if (real_hw_level > aio->filled_zeros_for_draining) {
    return real_hw_level - aio->filled_zeros_for_draining;
  }

  return 0;
}

/*
 * Exported Interface.
 */

struct cras_iodev* cras_alsa_usb_iodev_create(
    size_t card_index,
    const char* card_name,
    size_t device_index,
    const char* pcm_name,
    const char* dev_name,
    const char* dev_id,
    enum CRAS_ALSA_CARD_TYPE card_type,
    int is_first,
    struct cras_alsa_mixer* mixer,
    const struct cras_card_config* config,
    struct cras_use_case_mgr* ucm,
    snd_hctl_t* hctl,
    enum CRAS_STREAM_DIRECTION direction,
    size_t usb_vid,
    size_t usb_pid,
    char* usb_serial_number) {
  struct alsa_usb_io* aio;
  struct cras_iodev* iodev;

  if (direction != CRAS_STREAM_INPUT && direction != CRAS_STREAM_OUTPUT) {
    return NULL;
  }

  if (card_type != ALSA_CARD_TYPE_USB) {
    return NULL;
  }

  aio = (struct alsa_usb_io*)calloc(1, sizeof(*aio));
  if (!aio) {
    return NULL;
  }
  iodev = &aio->base;
  iodev->direction = direction;

  aio->device_index = device_index;
  aio->card_type = card_type;
  aio->is_first = is_first;
  aio->handle = NULL;
  aio->num_severe_underruns = 0;
  if (dev_name) {
    aio->dev_name = strdup(dev_name);
    if (!aio->dev_name) {
      goto cleanup_iodev;
    }
  }
  if (dev_id) {
    aio->dev_id = strdup(dev_id);
    if (!aio->dev_id) {
      goto cleanup_iodev;
    }
  }
  aio->free_running = 0;
  aio->filled_zeros_for_draining = 0;
  aio->has_dependent_dev = 0;
  aio->pcm_name = strdup(pcm_name);
  if (aio->pcm_name == NULL) {
    goto cleanup_iodev;
  }

  if (direction == CRAS_STREAM_INPUT) {
    aio->alsa_stream = SND_PCM_STREAM_CAPTURE;
    aio->base.set_capture_gain = usb_set_alsa_capture_gain;
    aio->base.set_capture_mute = usb_set_alsa_capture_gain;
  } else {
    aio->alsa_stream = SND_PCM_STREAM_PLAYBACK;
    aio->base.set_volume = usb_set_alsa_volume;
    aio->base.set_mute = usb_set_alsa_mute;
    aio->base.output_underrun = usb_alsa_output_underrun;
  }
  iodev->open_dev = usb_open_dev;
  iodev->configure_dev = usb_configure_dev;
  iodev->close_dev = usb_close_dev;
  iodev->update_supported_formats = usb_update_supported_formats;
  iodev->frames_queued = usb_frames_queued;
  iodev->delay_frames = usb_delay_frames;
  iodev->get_buffer = usb_get_buffer;
  iodev->put_buffer = usb_put_buffer;
  iodev->flush_buffer = usb_flush_buffer;
  iodev->start = usb_start;
  iodev->update_active_node = usb_update_active_node;
  iodev->update_channel_layout = usb_update_channel_layout;
  iodev->no_stream = usb_no_stream;
  iodev->is_free_running = usb_is_free_running;
  iodev->get_num_severe_underruns = usb_get_num_severe_underruns;
  iodev->get_valid_frames = usb_get_valid_frames;
  iodev->set_swap_mode_for_node = cras_iodev_dsp_set_swap_mode_for_node;
  iodev->set_display_rotation_for_node =
      cras_iodev_dsp_set_display_rotation_for_node;
  iodev->min_buffer_level = USB_EXTRA_BUFFER_FRAMES;

  iodev->ramp = cras_ramp_create();
  if (iodev->ramp == NULL) {
    goto cleanup_iodev;
  }
  iodev->initial_ramp_request = CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK;

  aio->mixer = mixer;
  aio->config = config;
  if (direction == CRAS_STREAM_OUTPUT) {
    aio->default_volume_curve =
        cras_card_config_get_volume_curve_for_control(config, "Default");
    // Default to max volume of 0dBFS, and a step of 0.5dBFS.
    if (aio->default_volume_curve == NULL) {
      aio->default_volume_curve = cras_volume_curve_create_default();
    }
  }
  aio->ucm = ucm;
  if (ucm) {
    unsigned int level;
    int rc;

    /* Set callback for swap mode if it is supported
     * in ucm modifier. */
    if (ucm_swap_mode_exists(ucm)) {
      aio->base.set_swap_mode_for_node = usb_set_alsa_node_swapped;
    }

    rc = ucm_get_min_buffer_level(ucm, &level);
    if (!rc && direction == CRAS_STREAM_OUTPUT) {
      iodev->min_buffer_level = level;
    }
  }

  usb_set_iodev_name(iodev, card_name, dev_name, card_index, device_index,
                     card_type, usb_vid, usb_pid, usb_serial_number);

  aio->jack_list = cras_alsa_jack_list_create(
      card_index, card_name, device_index, is_first, mixer, ucm, hctl,
      direction,
      direction == CRAS_STREAM_OUTPUT ? usb_jack_output_plug_event
                                      : usb_jack_input_plug_event,
      aio);
  if (!aio->jack_list) {
    goto cleanup_iodev;
  }

  /* Add this now so that cleanup of the iodev (in case of error or card
   * card removal will function as expected. */
  if (direction == CRAS_STREAM_OUTPUT) {
    cras_iodev_list_add_output(&aio->base);
  } else {
    cras_iodev_list_add_input(&aio->base);
  }
  return &aio->base;

cleanup_iodev:
  usb_free_alsa_iodev_resources(aio);
  free(aio);
  return NULL;
}

int cras_alsa_usb_iodev_legacy_complete_init(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  enum CRAS_STREAM_DIRECTION direction;
  int err;
  int is_first;
  struct cras_alsa_mixer* mixer;

  if (!aio) {
    return -EINVAL;
  }
  direction = iodev->direction;
  is_first = aio->is_first;
  mixer = aio->mixer;

  /* Create output nodes for mixer controls, such as Headphone
   * and Speaker, only for the first device. */
  if (direction == CRAS_STREAM_OUTPUT && is_first) {
    cras_alsa_mixer_list_outputs(mixer, usb_new_output_by_mixer_control, aio);
  } else if (direction == CRAS_STREAM_INPUT && is_first) {
    cras_alsa_mixer_list_inputs(mixer, usb_new_input_by_mixer_control, aio);
  }

  err = cras_alsa_jack_list_find_jacks_by_name_matching(aio->jack_list);
  if (err) {
    return err;
  }

  /* Create nodes for jacks that aren't associated with an
   * already existing node. Get an initial read of the jacks for
   * this device. */
  cras_alsa_jack_list_report(aio->jack_list);

  /* Make a default node if there is still no node for this
   * device, or we still don't have the "Speaker"/"Internal Mic"
   * node for the first internal device. Note that the default
   * node creation can be supressed by UCM flags for platforms
   * which really don't have an internal device. */
  if ((direction == CRAS_STREAM_OUTPUT) &&
      !usb_no_create_default_output_node(aio) && !aio->base.nodes) {
    usb_new_output(aio, NULL, DEFAULT);
  } else if ((direction == CRAS_STREAM_INPUT) &&
             !usb_no_create_default_input_node(aio) && !aio->base.nodes) {
    usb_new_input(aio, NULL, DEFAULT);
  }

  // Build software volume scalers.
  if (direction == CRAS_STREAM_OUTPUT) {
    usb_build_softvol_scalers(aio);
  }

  // Set the active node as the best node we have now.
  usb_alsa_iodev_set_active_node(&aio->base, first_plugged_node(&aio->base), 0);

  /* Set plugged for the first USB device per card when it appears if
   * there is no jack reporting plug status. */
  if (is_first && !usb_get_jack_from_node(iodev->active_node)) {
    cras_iodev_set_node_plugged(iodev->active_node, 1);
  }

  // Record max supported channels into cras_iodev_info.
  usb_update_max_supported_channels(iodev);

  return 0;
}

int cras_alsa_usb_iodev_ucm_add_nodes_and_jacks(struct cras_iodev* iodev,
                                                struct ucm_section* section) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  struct mixer_control* control;
  struct alsa_usb_input_node* input_node = NULL;
  struct cras_alsa_jack* jack;
  struct alsa_usb_output_node* output_node = NULL;
  int rc;

  if (!aio || !section) {
    return -EINVAL;
  }

  /* Allow this section to add as a new node only if the device id
   * or dependent device id matches this iodev. */
  if (((uint32_t)section->dev_idx != aio->device_index) &&
      ((uint32_t)section->dependent_dev_idx != aio->device_index)) {
    return -EINVAL;
  }

  // Set flag has_dependent_dev for the case of dependent device.
  if (section->dependent_dev_idx != -1) {
    aio->has_dependent_dev = 1;
  }

  // This iodev is fully specified. Avoid automatic node creation.
  aio->fully_specified = 1;

  /* Check here in case the DmaPeriodMicrosecs flag has only been
   * specified on one of many device entries with the same PCM. */
  if (aio->ucm && !aio->dma_period_set_microsecs) {
    aio->dma_period_set_microsecs =
        ucm_get_dma_period_for_dev(aio->ucm, section->name);
  }

  /* Create a node matching this section. If there is a matching
   * control use that, otherwise make a node without a control. */
  control = cras_alsa_mixer_get_control_for_section(aio->mixer, section);
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    output_node = usb_new_output(aio, control, section->name);
    if (!output_node) {
      return -ENOMEM;
    }
    output_node->pcm_name = strdup(section->pcm_name);
  } else if (iodev->direction == CRAS_STREAM_INPUT) {
    input_node = usb_new_input(aio, control, section->name);
    if (!input_node) {
      return -ENOMEM;
    }
    input_node->pcm_name = strdup(section->pcm_name);
  }

  // Find any jack controls for this device.
  rc = cras_alsa_jack_list_add_jack_for_section(aio->jack_list, section, &jack);
  if (rc) {
    return rc;
  }

  // Associated the jack with the node.
  if (jack) {
    if (output_node) {
      output_node->jack = jack;
      if (!output_node->volume_curve) {
        output_node->volume_curve =
            usb_create_volume_curve_for_jack(aio->config, jack);
      }
    } else if (input_node) {
      input_node->jack = jack;
    }
  }
  return 0;
}

void cras_alsa_usb_iodev_ucm_complete_init(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  struct cras_ionode* node;

  if (!iodev) {
    return;
  }

  // Get an initial read of the jacks for this device.
  cras_alsa_jack_list_report(aio->jack_list);

  // Build software volume scaler.
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    usb_build_softvol_scalers(aio);
  }

  // Set the active node as the best node we have now.
  usb_alsa_iodev_set_active_node(&aio->base, first_plugged_node(&aio->base), 0);

  /*
   * Set plugged for the USB device per card when it appears if
   * there is no jack reporting plug status
   */
  DL_FOREACH (iodev->nodes, node) {
    if (!usb_get_jack_from_node(node)) {
      cras_iodev_set_node_plugged(node, 1);
    }
  }

  node = iodev->active_node;
  if (node && node->plugged) {
    usb_update_max_supported_channels(iodev);
  }
}

void cras_alsa_usb_iodev_destroy(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  int rc;

  if (iodev->direction == CRAS_STREAM_INPUT) {
    rc = cras_iodev_list_rm_input(iodev);
  } else {
    rc = cras_iodev_list_rm_output(iodev);
  }

  if (rc == -EBUSY) {
    syslog(LOG_WARNING, "Failed to remove iodev %s", iodev->info.name);
    return;
  }

  // Free resources when device successfully removed.
  cras_alsa_jack_list_destroy(aio->jack_list);
  usb_free_alsa_iodev_resources(aio);
  cras_volume_curve_destroy(aio->default_volume_curve);
  free(iodev);
}

unsigned cras_alsa_usb_iodev_index(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  return aio->device_index;
}

int cras_alsa_usb_iodev_has_hctl_jacks(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  return cras_alsa_jack_list_has_hctl_jacks(aio->jack_list);
}

static void usb_alsa_iodev_unmute_node(struct alsa_usb_io* aio,
                                       struct cras_ionode* ionode) {
  struct alsa_usb_output_node* active = (struct alsa_usb_output_node*)ionode;
  struct mixer_control* mixer = active->mixer_output;
  struct alsa_usb_output_node* output;
  struct cras_ionode* node;

  /* If this node is associated with mixer output, unmute the
   * active mixer output and mute all others, otherwise just set
   * the node as active and set the volume curve. */
  if (mixer) {
    // Unmute the active mixer output, mute all others.
    DL_FOREACH (aio->base.nodes, node) {
      output = (struct alsa_usb_output_node*)node;
      if (output->mixer_output) {
        cras_alsa_mixer_set_output_active_state(output->mixer_output,
                                                node == ionode);
      }
    }
  }
}

static int usb_alsa_iodev_set_active_node(struct cras_iodev* iodev,
                                          struct cras_ionode* ionode,
                                          unsigned dev_enabled) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;

  if (iodev->active_node == ionode) {
    goto skip;
  }

  // Disable jack ucm before switching node.
  usb_enable_active_ucm(aio, 0);
  if (dev_enabled && (iodev->direction == CRAS_STREAM_OUTPUT)) {
    usb_alsa_iodev_unmute_node(aio, ionode);
  }

  cras_iodev_set_active_node(iodev, ionode);
  cras_iodev_update_dsp(iodev);
skip:
  usb_enable_active_ucm(aio, dev_enabled);
  // Setting the volume will also unmute if the system isn't muted.
  usb_init_device_settings(aio);
  return 0;
}

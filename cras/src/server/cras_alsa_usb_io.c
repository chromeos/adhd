/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras_alsa_usb_io.h"

#include <alsa/asoundlib.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "cras/common/check.h"
#include "cras/src/common/cras_alsa_card_info.h"
#include "cras/src/common/cras_log.h"
#include "cras/src/common/cras_metrics.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/config/cras_card_config.h"
#include "cras/src/server/cras_alsa_common_io.h"
#include "cras/src/server/cras_alsa_helpers.h"
#include "cras/src/server/cras_alsa_jack.h"
#include "cras/src/server/cras_alsa_mixer.h"
#include "cras/src/server/cras_alsa_ucm.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_ramp.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_utf8.h"
#include "cras/src/server/cras_volume_curve.h"
#include "cras/src/server/dev_stream.h"
#include "cras/src/server/softvol_curve.h"
#include "cras_audio_format.h"
#include "cras_iodev.h"
#include "cras_iodev_info.h"
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
  struct alsa_common_node common;
  // Volume curve for this node.
  struct cras_volume_curve* volume_curve;
};

struct alsa_usb_input_node {
  struct alsa_common_node common;
};

/*
 * Child of cras_iodev, alsa_usb_io handles ALSA interaction for sound devices.
 */
struct alsa_usb_io {
  // The alsa_io_common structure "base class".
  struct alsa_common_io common;
};

static void usb_init_device_settings(struct alsa_usb_io* aio);

static int usb_alsa_iodev_set_active_node(struct cras_iodev* iodev,
                                          struct cras_ionode* ionode,
                                          unsigned dev_enabled);

static int usb_update_supported_formats(struct cras_iodev* iodev);

static inline int usb_set_hwparams(struct cras_iodev* iodev) {
  return cras_alsa_common_set_hwparams(iodev, 0);
}

/*
 * iodev callbacks.
 */

static inline int usb_frames_queued(const struct cras_iodev* iodev,
                                    struct timespec* tstamp) {
  return cras_alsa_common_frames_queued(iodev, tstamp);
}

static inline int usb_delay_frames(const struct cras_iodev* iodev) {
  return cras_alsa_common_delay_frames(iodev);
}

static inline int usb_close_dev(struct cras_iodev* iodev) {
  struct timespec now, elapse;
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;

  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  subtract_timespecs(&now, &iodev->open_ts, &elapse);

  if (iodev->format != NULL) {
    audio_peripheral_close(aio->common.vendor_id, aio->common.product_id,
                           CRAS_NODE_TYPE_USB, elapse.tv_sec,
                           iodev->format->frame_rate,
                           iodev->format->num_channels, iodev->format->format);
  }

  return cras_alsa_common_close_dev(iodev);
}

static int usb_open_dev(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  const char* pcm_name = aio->common.pcm_name;

  aio->common.poll_fd = -1;
  audio_peripheral_info(aio->common.vendor_id, aio->common.product_id,
                        CRAS_NODE_TYPE_USB);

  return cras_alsa_common_open_dev(iodev, pcm_name);
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
  aio->common.free_running = 0;
  aio->common.filled_zeros_for_draining = 0;
  aio->common.severe_underrun_frames =
      SEVERE_UNDERRUN_MS * iodev->format->frame_rate / 1000;

  size_t fmt_bytes = cras_get_format_bytes(iodev->format);
  cras_iodev_init_audio_area(iodev);

  syslog(LOG_DEBUG,
         "card type: %s, Configure alsa device %s rate %zuHz, %zu channels",
         cras_card_type_to_string(aio->common.card_type), aio->common.pcm_name,
         iodev->format->frame_rate, iodev->format->num_channels);

  rc = usb_set_hwparams(iodev);
  if (rc < 0) {
    goto error_out;
  }

  if (!aio->common.sample_buf) {
    aio->common.sample_buf =
        (uint8_t*)calloc(iodev->buffer_size * fmt_bytes, sizeof(uint8_t));
    if (!aio->common.sample_buf) {
      syslog(LOG_ERR, "cras_alsa_io: configure_dev: calloc: %s",
             cras_strerror(errno));
      return -ENOMEM;
    }
    cras_audio_area_config_buf_pointers(iodev->area, iodev->format,
                                        aio->common.sample_buf);
  }

  // Set channel map to device
  rc = cras_alsa_set_channel_map(aio->common.handle, iodev->format);
  if (rc < 0) {
    goto error_out;
  }

  // Configure software params.
  rc = cras_alsa_set_swparams(aio->common.handle);
  if (rc < 0) {
    goto error_out;
  }

  // Initialize device settings.
  usb_init_device_settings(aio);

  // Capture starts right away, playback will wait for samples.
  if (aio->common.alsa_stream == SND_PCM_STREAM_CAPTURE) {
    rc = cras_alsa_pcm_start(aio->common.handle);
    if (rc < 0) {
      goto error_out;
    }
  }

  return 0;

error_out:

  FRALOG(USBAudioConfigureFailed,
         {"vid", tlsprintf("0x%04X", aio->common.vendor_id)},
         {"pid", tlsprintf("0x%04X", aio->common.product_id)},
         {"error", snd_strerror(rc)});

  syslog(LOG_ERR, "card type: %s, name: %s, Failed to configure_dev, ret: %s",
         cras_card_type_to_string(aio->common.card_type), iodev->info.name,
         snd_strerror(rc));
  return rc;
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
  return !!aio->common.handle;
}

static int usb_start(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  snd_pcm_t* handle = aio->common.handle;
  int rc;

  if (snd_pcm_state(handle) == SND_PCM_STATE_RUNNING) {
    return 0;
  }

  if (snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
    rc = cras_alsa_attempt_resume(handle);
    if (rc < 0) {
      FRALOG(USBAudioResumeFailed,
             {"vid", tlsprintf("0x%04X", aio->common.vendor_id)},
             {"pid", tlsprintf("0x%04X", aio->common.product_id)},
             {"error", snd_strerror(rc)});
      syslog(LOG_ERR, "card type: %s, name: %s, Resume error: %s",
             cras_card_type_to_string(aio->common.card_type), iodev->info.name,
             snd_strerror(rc));
      return rc;
    }
    cras_iodev_reset_rate_estimator(iodev);
  } else {
    rc = cras_alsa_pcm_start(handle);
    if (rc < 0) {
      FRALOG(USBAudioStartFailed,
             {"vid", tlsprintf("0x%04X", aio->common.vendor_id)},
             {"pid", tlsprintf("0x%04X", aio->common.product_id)},
             {"error", snd_strerror(rc)});
      syslog(LOG_ERR, "card type: %s, name: %s, Start error: %s",
             cras_card_type_to_string(aio->common.card_type), iodev->info.name,
             snd_strerror(rc));
      return rc;
    }
  }

  return 0;
}

static int usb_get_buffer(struct cras_iodev* iodev,
                          struct cras_audio_area** area,
                          unsigned* frames) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  snd_pcm_uframes_t nframes = MIN(iodev->buffer_size, *frames);

  aio->common.mmap_offset = 0;
  size_t format_bytes = cras_get_format_bytes(iodev->format);

  int rc = cras_alsa_mmap_begin(aio->common.handle, format_bytes,
                                &aio->common.mmap_buf, &aio->common.mmap_offset,
                                &nframes);
  if (rc < 0) {
    aio->common.mmap_buf = NULL;
    return rc;
  }
  iodev->area->frames = nframes;
  // Copy mmap_buf data to local memory for faster manipulation.
  // Check `cras_bench --benchmark_filter=BM_Alsa/MmapBuffer` for analysis.
  if (iodev->direction == CRAS_STREAM_INPUT) {
    if (nframes > iodev->input_dsp_offset) {
      memcpy(aio->common.sample_buf + iodev->input_dsp_offset * format_bytes,
             aio->common.mmap_buf + iodev->input_dsp_offset * format_bytes,
             (nframes - iodev->input_dsp_offset) * format_bytes);
    }
  }

  *area = iodev->area;
  *frames = nframes;

  return rc;
}

static int usb_put_buffer(struct cras_iodev* iodev, unsigned nwritten) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;

  size_t format_bytes = cras_get_format_bytes(iodev->format);
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    memcpy(aio->common.mmap_buf, aio->common.sample_buf,
           (size_t)nwritten * (size_t)format_bytes);
    unsigned int max_offset = cras_iodev_max_stream_offset(iodev);
    if (max_offset) {
      memmove(aio->common.sample_buf,
              aio->common.sample_buf + nwritten * format_bytes,
              max_offset * format_bytes);
    }
  } else {
    // CRAS applied input DSP on the uncommitted data, move then to the
    // beginning.
    if (iodev->input_dsp_offset) {
      memmove(aio->common.sample_buf,
              aio->common.sample_buf + nwritten * format_bytes,
              iodev->input_dsp_offset * format_bytes);
    }
  }
  return cras_alsa_mmap_commit(aio->common.handle, aio->common.mmap_offset,
                               nwritten);
}

static int usb_flush_buffer(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  snd_pcm_uframes_t nframes;

  if (iodev->direction == CRAS_STREAM_INPUT) {
    nframes = snd_pcm_avail(aio->common.handle);
    nframes = snd_pcm_forwardable(aio->common.handle);
    return snd_pcm_forward(aio->common.handle, nframes);
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
  if (aio->common.ucm) {
    struct alsa_common_node* node =
        (struct alsa_common_node*)iodev->active_node;
    if (node->channel_layout) {
      memcpy(iodev->format->channel_layout, node->channel_layout,
             CRAS_CH_MAX * sizeof(*node->channel_layout));
      return 0;
    }
  }

  err = usb_set_hwparams(iodev);
  if (err < 0) {
    return err;
  }

  return cras_alsa_get_channel_map(aio->common.handle, iodev->format);
}

/*
 * Alsa helper functions.
 */

static struct alsa_usb_output_node* usb_get_active_output(
    const struct alsa_usb_io* aio) {
  return (struct alsa_usb_output_node*)aio->common.base.active_node;
}

static struct alsa_usb_input_node* usb_get_active_input(
    const struct alsa_usb_io* aio) {
  return (struct alsa_usb_input_node*)aio->common.base.active_node;
}

/*
 * Gets the curve for the active output node. If the node doesn't have volume
 * curve specified, return the default volume curve of the common iodev.
 */
static const struct cras_volume_curve* usb_get_curve_for_output_node(
    const struct alsa_usb_io* aio,
    const struct alsa_usb_output_node* node) {
  if (node && node->volume_curve) {
    return node->volume_curve;
  }
  return aio->common.default_volume_curve;
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

  CRAS_CHECK(aio);
  if (aio->common.mixer == NULL) {
    return;
  }

  volume = cras_system_get_volume();
  curve = usb_get_curve_for_active_output(aio);
  if (curve == NULL) {
    return;
  }
  aout = usb_get_active_output(aio);
  if (aout) {
    volume = cras_iodev_adjust_node_volume(&aout->common.base, volume);
  }

  /* Samples get scaled for devices using software volume, set alsa
   * volume to 100. */
  if (cras_iodev_software_volume_needed(iodev)) {
    volume = 100;
  }

  cras_alsa_mixer_set_dBFS(aio->common.mixer, curve->get_dBFS(curve, volume),
                           aout ? aout->common.mixer : NULL);
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
  cras_alsa_mixer_set_mute(aio->common.mixer, cras_system_get_mute(),
                           aout ? aout->common.mixer : NULL);
}

/* Sets the capture gain based on the internal gain value configured on
 * active node. It could be HW or SW gain decided by the logic behind
 * |cras_iodev_software_volume_needed|.
 */
static void usb_set_alsa_capture_gain(struct cras_iodev* iodev) {
  const struct alsa_usb_io* aio = (const struct alsa_usb_io*)iodev;
  struct alsa_usb_input_node* ain;
  long min_capture_gain, max_capture_gain, gain;
  CRAS_CHECK(aio);
  if (aio->common.mixer == NULL) {
    return;
  }

  // Only set the volume if the dev is active.
  if (!usb_has_handle(aio)) {
    return;
  }

  ain = usb_get_active_input(aio);

  // For USB device without UCM config, not change a gain control.
  if (!aio->common.ucm) {
    return;
  }

  struct mixer_control* mixer = ain ? ain->common.mixer : NULL;

  // Set hardware gain to 0dB if software gain is needed.
  if (cras_iodev_software_volume_needed(iodev)) {
    gain = 0;
  } else {
    min_capture_gain =
        cras_alsa_mixer_get_minimum_capture_gain(aio->common.mixer, mixer);
    max_capture_gain =
        cras_alsa_mixer_get_maximum_capture_gain(aio->common.mixer, mixer);
    gain = MAX(iodev->active_node->internal_capture_gain, min_capture_gain);
    gain = MIN(gain, max_capture_gain);
  }

  cras_alsa_mixer_set_capture_dBFS(aio->common.mixer, gain, mixer);
}

/*
 * Swaps the left and right channels of the given node.
 */
static int usb_set_alsa_node_swapped(struct cras_iodev* iodev,
                                     struct cras_ionode* node,
                                     int enable) {
  const struct alsa_usb_io* aio = (const struct alsa_usb_io*)iodev;
  const struct alsa_common_node* anode = (const struct alsa_common_node*)node;
  CRAS_CHECK(aio);
  return ucm_enable_swap_mode(aio->common.ucm, anode->ucm_name, enable);
}

/*
 * Initializes the device settings according to system volume, mute, gain
 * settings.
 * Updates system capture gain limits based on current active device/node.
 */
static void usb_init_device_settings(struct alsa_usb_io* aio) {
  /* Register for volume/mute callback and set initial volume/mute for
   * the device. */
  if (aio->common.base.direction == CRAS_STREAM_OUTPUT) {
    usb_set_alsa_volume_limits(aio);
    usb_set_alsa_volume(&aio->common.base);
    usb_set_alsa_mute(&aio->common.base);
  } else {
    usb_set_alsa_capture_gain(&aio->common.base);
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

  free(aio->common.base.supported_rates);
  free(aio->common.base.supported_channel_counts);
  free(aio->common.base.supported_formats);

  DL_FOREACH (aio->common.base.nodes, node) {
    if (aio->common.base.direction == CRAS_STREAM_OUTPUT) {
      struct alsa_usb_output_node* aout = (struct alsa_usb_output_node*)node;
      cras_volume_curve_destroy(aout->volume_curve);
    }

    cras_iodev_rm_node(&aio->common.base, node);
    free(node->softvol_scalers);
    free((void*)node->dsp_name);
    free(node);
  }

  cras_iodev_free_resources(&aio->common.base);
  free(aio->common.pcm_name);
  if (aio->common.dev_id) {
    free(aio->common.dev_id);
  }
  if (aio->common.dev_name) {
    free(aio->common.dev_name);
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

  if (!aio->common.ucm) {
    return -ENOENT;
  }

  value = ucm_get_flag(aio->common.ucm, flag_name);
  if (!value) {
    return -EINVAL;
  }

  int rc = parse_int(value, &i);
  free(value);
  if (rc < 0) {
    return rc;
  }
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

static void usb_set_input_default_node_gain(struct alsa_usb_input_node* input,
                                            struct alsa_usb_io* aio) {
  long gain;

  input->common.base.internal_capture_gain = DEFAULT_CAPTURE_GAIN;
  input->common.base.ui_gain_scaler = 1.0f;

  if (!aio->common.ucm) {
    return;
  }

  if (ucm_get_default_node_gain(aio->common.ucm, input->common.ucm_name,
                                &gain) == 0) {
    input->common.base.internal_capture_gain = gain;
  }
}

static void usb_set_input_node_intrinsic_sensitivity(
    struct alsa_usb_input_node* input,
    struct alsa_usb_io* aio) {
  struct cras_ionode* node = &input->common.base;
  long sensitivity;
  int rc;

  node->intrinsic_sensitivity = 0;

  if (aio->common.ucm) {
    rc = ucm_get_intrinsic_sensitivity(aio->common.ucm, input->common.ucm_name,
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
  node->intrinsic_sensitivity = sensitivity;
  node->internal_capture_gain = DEFAULT_CAPTURE_VOLUME_DBFS - sensitivity;
  syslog(LOG_INFO,
         "card type: %s, Use software gain %ld for %s because "
         "IntrinsicSensitivity %ld is"
         " specified in UCM",
         cras_card_type_to_string(aio->common.card_type),
         node->internal_capture_gain, node->name, sensitivity);
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
    DL_FOREACH (aio->common.base.nodes, tmp) {
      if (tmp->plugged && (tmp != node)) {
        cras_iodev_set_node_plugged(node, 0);
      }
    }
  } else {
    DL_FOREACH (aio->common.base.nodes, tmp) {
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
  CRAS_CHECK(name);

  int err;
  syslog(LOG_DEBUG, "card type: %s, New output node for '%s'",
         cras_card_type_to_string(aio->common.card_type), name);
  if (aio == NULL) {
    FRALOG(USBAudioListOutputNodeFailed, {"name", aio->common.base.info.name});
    syslog(LOG_ERR,
           "card type: %s, name: %s, Invalid aio when listing outputs.",
           aio->common.base.info.name,
           cras_card_type_to_string(aio->common.card_type));
    return NULL;
  }
  struct alsa_usb_output_node* output =
      (struct alsa_usb_output_node*)calloc(1, sizeof(*output));
  struct cras_ionode* node = (struct cras_ionode*)&output->common.base;
  if (output == NULL) {
    syslog(LOG_ERR, "card type: %s, Out of memory when listing outputs.",
           cras_card_type_to_string(aio->common.card_type));
    return NULL;
  }
  node->dev = &aio->common.base;
  node->idx = aio->common.next_ionode_index++;
  node->stable_id =
      SuperFastHash(name, strlen(name), aio->common.base.info.stable_id);

  if (aio->common.ucm) {
    // Check if channel map is specified in UCM.
    output->common.channel_layout =
        (int8_t*)malloc(CRAS_CH_MAX * sizeof(*output->common.channel_layout));
    err = ucm_get_playback_chmap_for_dev(aio->common.ucm, name,
                                         output->common.channel_layout);
    if (err) {
      free(output->common.channel_layout);
      output->common.channel_layout = 0;
    }

    node->dsp_name = ucm_get_dsp_name_for_dev(aio->common.ucm, name);
  }
  output->common.mixer = cras_control;

  strlcpy(node->name, name, sizeof(node->name));
  strlcpy(output->common.ucm_name, name, sizeof(output->common.ucm_name));
  usb_set_node_initial_state(node);

  cras_iodev_add_node(&aio->common.base, node);

  usb_check_auto_unplug_output_node(aio, node, node->plugged);
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
  if (snprintf(node_name, sizeof(node_name), "%s: %s",
               aio->common.base.info.name, ctl_name) > 0) {
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
    DL_FOREACH (aio->common.base.nodes, tmp) {
      if (tmp->plugged && (tmp != node)) {
        cras_iodev_set_node_plugged(node, 0);
      }
    }
  } else {
    DL_FOREACH (aio->common.base.nodes, tmp) {
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
  struct cras_iodev* iodev = &aio->common.base;
  int err;

  struct alsa_usb_input_node* input =
      (struct alsa_usb_input_node*)calloc(1, sizeof(*input));
  if (input == NULL) {
    syslog(LOG_ERR, "card type: %s, Out of memory when listing inputs.",
           cras_card_type_to_string(aio->common.card_type));
    return NULL;
  }
  struct cras_ionode* node = &input->common.base;
  node->dev = &aio->common.base;
  node->idx = aio->common.next_ionode_index++;
  node->stable_id =
      SuperFastHash(name, strlen(name), aio->common.base.info.stable_id);
  input->common.mixer = cras_input;
  strlcpy(node->name, name, sizeof(node->name));
  strlcpy(input->common.ucm_name, name, sizeof(input->common.ucm_name));
  usb_set_node_initial_state(node);
  usb_set_input_default_node_gain(input, aio);
  usb_set_input_node_intrinsic_sensitivity(input, aio);

  if (aio->common.ucm) {
    // Check if channel map is specified in UCM.
    input->common.channel_layout =
        (int8_t*)malloc(CRAS_CH_MAX * sizeof(*input->common.channel_layout));
    err = ucm_get_capture_chmap_for_dev(aio->common.ucm, name,
                                        input->common.channel_layout);
    if (err) {
      free(input->common.channel_layout);
      input->common.channel_layout = 0;
    }
    if (ucm_get_preempt_hotword(aio->common.ucm, name)) {
      iodev->pre_open_iodev_hook = cras_iodev_list_suspend_hotword_streams;
      iodev->post_close_iodev_hook = cras_iodev_list_resume_hotword_stream;
    }

    node->dsp_name = ucm_get_dsp_name_for_dev(aio->common.ucm, name);
  }

  // Set NC provider.
  node->nc_providers = cras_alsa_common_get_nc_providers(aio->common.ucm, node);

  cras_iodev_add_node(&aio->common.base, node);
  usb_check_auto_unplug_input_node(aio, node, node->plugged);
  return input;
}

static void usb_new_input_by_mixer_control(struct mixer_control* cras_input,
                                           void* callback_arg) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)callback_arg;
  char node_name[CRAS_IODEV_NAME_BUFFER_SIZE];
  const char* ctl_name = cras_alsa_mixer_get_control_name(cras_input);
  int ret = snprintf(node_name, sizeof(node_name), "%s: %s",
                     aio->common.base.info.name, ctl_name);
  // Truncation is OK, but add a check to make the compiler happy.
  if (ret == sizeof(node_name)) {
    node_name[sizeof(node_name) - 1] = '\0';
  }
  usb_new_input(aio, cras_input, node_name);
}

static const struct cras_alsa_jack* usb_get_jack_from_node(
    struct cras_ionode* node) {
  const struct alsa_common_node* anode = (struct alsa_common_node*)node;

  if (node == NULL) {
    return NULL;
  }

  return anode->jack;
}

/*
 * Creates volume curve for the node associated with given output
 * usb node.
 */
static struct cras_volume_curve* usb_create_volume_curve_for_output(
    const struct cras_card_config* config,
    const struct alsa_usb_output_node* aout) {
  struct cras_volume_curve* curve;
  const struct alsa_common_node* anode = &aout->common;
  const char* name;

  // Use node's name as key to get volume curve.
  name = anode->base.name;
  curve = cras_card_config_get_volume_curve_for_control(config, name);
  if (curve) {
    return curve;
  }

  if (anode->jack == NULL) {
    return NULL;
  }

  // Use jack's UCM device name as key to get volume curve.
  name = cras_alsa_jack_get_ucm_device(anode->jack);
  curve = cras_card_config_get_volume_curve_for_control(config, name);
  if (curve) {
    return curve;
  }

  // Use alsa jack's name as key to get volume curve.
  name = cras_alsa_jack_get_name(anode->jack);
  return cras_card_config_get_volume_curve_for_control(config, name);
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
  if (aio->common.has_dependent_dev) {
    max_channels = 2;
    goto update_info;
  }

  if (aio->common.handle) {
    syslog(
        LOG_ERR,
        "card type: %s, usb_update_max_supported_channels should not be called "
        "while device is opened.",
        cras_card_type_to_string(aio->common.card_type));
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
    syslog(LOG_DEBUG,
           "card type: %s, Predict ionode %s as active node temporarily.",
           cras_card_type_to_string(aio->common.card_type),
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
  if (arg == NULL) {
    return;
  }

  struct alsa_usb_io* aio = (struct alsa_usb_io*)arg;
  struct alsa_usb_output_node* aout =
      (struct alsa_usb_output_node*)cras_alsa_get_node_from_jack(&aio->common,
                                                                 jack);
  struct alsa_common_node* anode = &aout->common;
  const char* jack_name = cras_alsa_jack_get_name(jack);
  if (!jack_name || !strcmp(jack_name, "Speaker Phantom Jack")) {
    jack_name = INTERNAL_SPEAKER;
  }

  syslog(LOG_DEBUG, "card type: %s, %s plugged: %d, %s",
         cras_card_type_to_string(aio->common.card_type), jack_name, plugged,
         cras_alsa_mixer_get_control_name(anode->mixer));

  cras_alsa_jack_update_monitor_name(jack, anode->base.name,
                                     sizeof(anode->base.name));
  // The name got from jack might be an invalid UTF8 string.
  if (!is_utf8_string(anode->base.name)) {
    usb_drop_node_name(&anode->base);
  }

  cras_iodev_set_node_plugged(&anode->base, plugged);

  usb_check_auto_unplug_output_node(aio, &anode->base, plugged);
}

/*
 * Callback that is called when an input jack is plugged or unplugged.
 */
static void usb_jack_input_plug_event(const struct cras_alsa_jack* jack,
                                      int plugged,
                                      void* arg) {
  if (arg == NULL) {
    return;
  }
  struct alsa_usb_io* aio = (struct alsa_usb_io*)arg;
  struct alsa_common_node* ain =
      cras_alsa_get_node_from_jack(&aio->common, jack);
  struct cras_ionode* node = &ain->base;
  const char* jack_name = cras_alsa_jack_get_name(jack);

  syslog(LOG_DEBUG, "card type: %s, %s plugged: %d, %s",
         cras_card_type_to_string(aio->common.card_type), jack_name, plugged,
         cras_alsa_mixer_get_control_name(ain->mixer));

  cras_iodev_set_node_plugged(node, plugged);

  usb_check_auto_unplug_input_node(aio, node, plugged);
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
                               const char* usb_serial_number) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)dev;
  snprintf(dev->info.name, sizeof(dev->info.name), "%s: %s:%zu,%zu", card_name,
           dev_name, card_index, device_index);
  dev->info.name[ARRAY_SIZE(dev->info.name) - 1] = '\0';

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

  aio->common.vendor_id = usb_vid;
  aio->common.product_id = usb_pid;

  FRALOG(PeripheralsUsbSoundCard, {"deviceName", dev->info.name},
         {"vid", tlsprintf("0x%04X", usb_vid)},
         {"pid", tlsprintf("0x%04X", usb_pid)});
  syslog(LOG_INFO,
         "Add cardType=USB, deviceName=%s, idVendor=0x%zx, idProduct=0x%zx, "
         "direction=%s",
         dev->info.name, usb_vid, usb_pid,
         dev->direction == CRAS_STREAM_OUTPUT ? "output" : "input");
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

  err = cras_alsa_fill_properties(aio->common.handle, &iodev->supported_rates,
                                  &iodev->supported_channel_counts,
                                  &iodev->supported_formats);
  if (err) {
    return err;
  }

  if (aio->common.ucm) {
    // Allow UCM to override supplied rates.
    fixed_rate = cras_alsa_get_fixed_rate(&aio->common);
    if (fixed_rate > 0) {
      free(iodev->supported_rates);
      iodev->supported_rates =
          (size_t*)malloc(2 * sizeof(iodev->supported_rates[0]));
      iodev->supported_rates[0] = fixed_rate;
      iodev->supported_rates[1] = 0;
    }

    // Allow UCM to override supported channel counts.
    fixed_channels = cras_alsa_get_fixed_channels(&aio->common);
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

static void usb_enable_active_ucm(struct alsa_usb_io* aio, int plugged) {
  struct alsa_common_node* anode =
      (struct alsa_common_node*)aio->common.base.active_node;

  if (!anode) {
    return;
  }
  const char* name = anode->ucm_name;
  const struct cras_alsa_jack* jack = anode->jack;

  if (jack) {
    cras_alsa_jack_enable_ucm(jack, plugged);
  } else if (aio->common.ucm) {
    ucm_set_enabled(aio->common.ucm, name, plugged);
  }
}

static int usb_fill_whole_buffer_with_zeros(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  int rc;
  uint8_t* dst = NULL;
  size_t format_bytes;

  // Fill whole buffer with zeros.
  rc = cras_alsa_mmap_get_whole_buffer(aio->common.handle, &dst);

  if (rc < 0) {
    syslog(LOG_WARNING,
           "card type: %s, name:%s, Failed to get whole buffer: %s",
           cras_card_type_to_string(aio->common.card_type),
           aio->common.base.info.name, snd_strerror(rc));
    return rc;
  }

  format_bytes = cras_get_format_bytes(iodev->format);
  memset(dst, 0, iodev->buffer_size * format_bytes);
  cras_iodev_stream_offset_reset_all(iodev);

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
  return cras_alsa_resume_appl_ptr(aio->common.handle, ahead, NULL);
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
  rc = cras_alsa_resume_appl_ptr(aio->common.handle, ahead,
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

  if (real_hw_level > aio->common.filled_zeros_for_draining) {
    valid_sample = real_hw_level - aio->common.filled_zeros_for_draining;
  }

  offset = MAX(odev->min_buffer_level + odev->min_cb_level, valid_sample);

  // Fill zeros to make sure there are enough zero samples in device buffer.
  if (offset > real_hw_level) {
    rc = cras_iodev_fill_odev_zeros(odev, offset - real_hw_level, true);
    if (rc < 0) {
      return rc;
    }
  }
  return cras_alsa_resume_appl_ptr(aio->common.handle, offset, NULL);
}

static int usb_alsa_output_underrun(struct cras_iodev* odev) {
  int rc, filled_frames;

  /* Fill whole buffer with zeros. This avoids samples left in buffer causing
   * noise when device plays them. */
  filled_frames = usb_fill_whole_buffer_with_zeros(odev);
  if (filled_frames < 0) {
    return filled_frames;
  }

  // Adjust appl_ptr to leave underrun.
  rc = usb_adjust_appl_ptr_for_underrun(odev);
  if (rc < 0) {
    return rc;
  }

  return filled_frames;
}

static int usb_possibly_enter_free_run(struct cras_iodev* odev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)odev;
  int rc;
  unsigned int real_hw_level, fr_to_write;
  struct timespec hw_tstamp;

  if (aio->common.free_running) {
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
    aio->common.free_running = 1;
    return 0;
  }

  if (real_hw_level <= aio->common.filled_zeros_for_draining ||
      real_hw_level == 0) {
    rc = usb_fill_whole_buffer_with_zeros(odev);
    if (rc < 0) {
      return rc;
    }
    aio->common.free_running = 1;
    return 0;
  }

  // Fill zeros to drain valid samples.
  fr_to_write = MIN(cras_time_to_frames(&no_stream_fill_zeros_duration,
                                        odev->format->frame_rate),
                    odev->buffer_size - real_hw_level);
  rc = cras_iodev_fill_odev_zeros(odev, fr_to_write, true);
  if (rc < 0) {
    return rc;
  }
  aio->common.filled_zeros_for_draining += fr_to_write;

  return 0;
}

static int usb_leave_free_run(struct cras_iodev* odev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)odev;
  int rc;

  /* Restart rate estimation because free run internval should not
   * be included. */
  cras_iodev_reset_rate_estimator(odev);

  if (aio->common.free_running) {
    rc = usb_adjust_appl_ptr_for_leaving_free_run(odev);
  } else {
    rc = usb_adjust_appl_ptr_samples_remaining(odev);
  }
  if (rc < 0) {
    syslog(LOG_WARNING,
           "card type: %s, device %s failed to leave free run, rc = %d",
           cras_card_type_to_string(aio->common.card_type), odev->info.name,
           rc);
    return rc;
  }
  aio->common.free_running = 0;
  aio->common.filled_zeros_for_draining = 0;

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

  return aio->common.free_running;
}

static unsigned int usb_get_num_severe_underruns(
    const struct cras_iodev* iodev) {
  const struct alsa_usb_io* aio = (const struct alsa_usb_io*)iodev;
  return aio->common.num_severe_underruns;
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
  if (aio->common.free_running) {
    clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
    return 0;
  }

  rc = odev->frames_queued(odev, tstamp);
  if (rc < 0) {
    return rc;
  }
  real_hw_level = rc;

  if (real_hw_level > aio->common.filled_zeros_for_draining) {
    return real_hw_level - aio->common.filled_zeros_for_draining;
  }

  return 0;
}

/*
 * Exported Interface.
 */

struct cras_iodev* cras_alsa_usb_iodev_create(
    const struct cras_alsa_card_info* card_info,
    const char* card_name,
    size_t device_index,
    const char* pcm_name,
    const char* dev_name,
    const char* dev_id,
    int is_first,
    struct cras_alsa_mixer* mixer,
    const struct cras_card_config* config,
    struct cras_use_case_mgr* ucm,
    snd_hctl_t* hctl,
    enum CRAS_STREAM_DIRECTION direction,
    enum CRAS_USE_CASE use_case,
    struct cras_iodev* group_ref) {
  struct alsa_usb_io* aio;
  struct cras_iodev* iodev;
  const struct cras_alsa_usb_card_info* usb_card_info;

  if (direction != CRAS_STREAM_INPUT && direction != CRAS_STREAM_OUTPUT) {
    return NULL;
  }

  if (!card_info || card_info->card_type != ALSA_CARD_TYPE_USB) {
    return NULL;
  }

  usb_card_info = cras_alsa_usb_card_info_get(card_info);

  aio = (struct alsa_usb_io*)calloc(1, sizeof(*aio));
  if (!aio) {
    return NULL;
  }
  iodev = &aio->common.base;
  iodev->direction = direction;

  aio->common.device_index = device_index;
  aio->common.card_type = card_info->card_type;
  aio->common.is_first = is_first;
  aio->common.handle = NULL;
  aio->common.num_severe_underruns = 0;
  if (dev_name) {
    aio->common.dev_name = strdup(dev_name);
    if (!aio->common.dev_name) {
      goto cleanup_iodev;
    }
  }
  if (dev_id) {
    aio->common.dev_id = strdup(dev_id);
    if (!aio->common.dev_id) {
      goto cleanup_iodev;
    }
  }
  aio->common.free_running = 0;
  aio->common.filled_zeros_for_draining = 0;
  aio->common.has_dependent_dev = 0;
  aio->common.pcm_name = strdup(pcm_name);
  if (aio->common.pcm_name == NULL) {
    goto cleanup_iodev;
  }

  if (direction == CRAS_STREAM_INPUT) {
    aio->common.alsa_stream = SND_PCM_STREAM_CAPTURE;
  } else {
    aio->common.alsa_stream = SND_PCM_STREAM_PLAYBACK;
    aio->common.base.set_volume = usb_set_alsa_volume;
    aio->common.base.set_mute = usb_set_alsa_mute;
    aio->common.base.output_underrun = usb_alsa_output_underrun;
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
  iodev->get_htimestamp = cras_alsa_common_get_htimestamp;
  iodev->min_buffer_level = USB_EXTRA_BUFFER_FRAMES;

  iodev->ramp = cras_ramp_create();
  if (iodev->ramp == NULL) {
    goto cleanup_iodev;
  }
  iodev->initial_ramp_request = CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK;

  aio->common.mixer = mixer;
  aio->common.config = config;
  if (direction == CRAS_STREAM_OUTPUT) {
    aio->common.default_volume_curve =
        cras_card_config_get_volume_curve_for_control(config, "Default");
    // Default to max volume of 0dBFS, and a step of 0.5dBFS.
    if (aio->common.default_volume_curve == NULL) {
      aio->common.default_volume_curve = cras_volume_curve_create_default();
    }
  }
  aio->common.ucm = ucm;
  if (ucm) {
    unsigned int level;
    int rc;

    /* Set callback for swap mode if it is supported
     * in ucm modifier. */
    if (ucm_swap_mode_exists(ucm)) {
      aio->common.base.set_swap_mode_for_node = usb_set_alsa_node_swapped;
    }

    rc = ucm_get_min_buffer_level(ucm, &level);
    if (!rc && direction == CRAS_STREAM_OUTPUT) {
      iodev->min_buffer_level = level;
    }
  }

  usb_set_iodev_name(
      iodev, card_name, dev_name, card_info->card_index, device_index,
      card_info->card_type, usb_card_info->usb_vendor_id,
      usb_card_info->usb_product_id, usb_card_info->usb_serial_number);

  aio->common.jack_list = cras_alsa_jack_list_create(
      card_info->card_index, card_name, device_index, is_first, mixer, ucm,
      hctl, direction,
      direction == CRAS_STREAM_OUTPUT ? usb_jack_output_plug_event
                                      : usb_jack_input_plug_event,
      aio);
  if (!aio->common.jack_list) {
    goto cleanup_iodev;
  }

  /* Add this now so that cleanup of the iodev (in case of error or card
   * card removal will function as expected. */
  cras_iodev_list_add(&aio->common.base);
  return &aio->common.base;

cleanup_iodev:
  usb_free_alsa_iodev_resources(aio);
  free(aio);
  return NULL;
}

// When a jack is found, try to associate it with a node already created
// for mixer control. If there isn't a node can be associated, go for
// creating a new node for the jack.
static void add_input_node_and_associate_jack(const struct cras_alsa_jack* jack,
                                              void* arg) {
  struct alsa_usb_io* aio;
  struct alsa_common_node* node;
  struct mixer_control* cras_input;
  const char* jack_name;

  CRAS_CHECK(arg);

  aio = (struct alsa_usb_io*)arg;
  node = cras_alsa_get_node_from_jack(&aio->common, jack);
  jack_name = cras_alsa_jack_get_name(jack);

  // If there isn't a node for this jack, create one.
  if (node == NULL) {
    cras_input = cras_alsa_jack_get_mixer(jack);
    node = (struct alsa_common_node*)usb_new_input(aio, cras_input, jack_name);
    if (node == NULL) {
      return;
    }
  }

  // If we already have the node, associate with the jack.
  if (!node->jack) {
    node->jack = jack;
  }
}

static void add_output_node_and_associate_jack(
    const struct cras_alsa_jack* jack,
    void* arg) {
  struct alsa_usb_io* aio;
  struct alsa_common_node* node;
  const char* jack_name;

  CRAS_CHECK(arg);

  aio = (struct alsa_usb_io*)arg;
  node = cras_alsa_get_node_from_jack(&aio->common, jack);
  jack_name = cras_alsa_jack_get_name(jack);
  if (!jack_name || !strcmp(jack_name, "Speaker Phantom Jack")) {
    jack_name = INTERNAL_SPEAKER;
  }

  // If there isn't a node for this jack, create one.
  if (node == NULL) {
    node = (struct alsa_common_node*)usb_new_output(aio, NULL, jack_name);
    if (node == NULL) {
      return;
    }

    cras_alsa_jack_update_node_type(jack, &(node->base.type));
  }

  if (!node->jack) {
    // If we already have the node, associate with the jack.
    node->jack = jack;
  }
}
/*
 * If volume range abnormal (< 5db or volume range > 200), then use SW volume.
 * Base on go/refine-cros-playback-vol
 *   If volume step < 10, then use SW volume and 25 volume steps
 *   If 10 <= volume step <= 25, then use HW volume and device reported steps
 *   If volume step >= 25, then use HW volume and 25 volume steps
 */

static void configure_default_volume_settings(
    struct alsa_usb_output_node* output,
    struct alsa_usb_io* aio,
    long min,
    long max) {
  struct cras_ionode* node = &output->common.base;
  int number_of_volume_steps = 0;
  long range = max - min;
  bool vol_range_reasonable = ((range >= db_to_alsa_db(VOLUME_RANGE_DB_MIN)) &&
                               (range <= db_to_alsa_db(VOLUME_RANGE_DB_MAX)));

  node->software_volume_needed = 0;
  node->number_of_volume_steps = NUMBER_OF_VOLUME_STEPS_DEFAULT;

  number_of_volume_steps =
      MIN(cras_alsa_mixer_get_playback_step(output->common.mixer),
          NUMBER_OF_VOLUME_STEPS_MAX);
  if (number_of_volume_steps < NUMBER_OF_VOLUME_STEPS_MIN) {
    FRALOG(USBAudioSoftwareVolumeAbnormalSteps,
           {"vid", tlsprintf("0x%04X", aio->common.vendor_id)},
           {"pid", tlsprintf("0x%04X", aio->common.product_id)});
    syslog(LOG_WARNING,
           "card type: %s, name: %s, output number_of_volume_steps [%" PRId32
           "] is abnormally small."
           " Fallback to software volume",
           cras_card_type_to_string(aio->common.card_type), node->name,
           number_of_volume_steps);
    node->software_volume_needed = 1;
  } else if (!vol_range_reasonable) {
    FRALOG(USBAudioSoftwareVolumeAbnormalRange,
           {"vid", tlsprintf("0x%04X", aio->common.vendor_id)},
           {"pid", tlsprintf("0x%04X", aio->common.product_id)});
    syslog(LOG_WARNING,
           "card type: %s, name: %s, output volume range [%ld %ld] is abnormal."
           "Fallback to software volume",
           cras_card_type_to_string(aio->common.card_type), node->name, min,
           max);
    node->software_volume_needed = 1;
  } else {
    /* Hardware volume is decided to be used in this case. */
    node->number_of_volume_steps = number_of_volume_steps;
  }
}
/* Only call this function if USB soundcard have ucm. When explicitly specify
 * UseSoftwareVolume = 1 CRAS will use 25 volume steps and SW volume. When UCM
 * don't explicitly specify UseSoftwareVolume = 1, CRAS always use device
 * reported steps and HW volume. If HW volume granularity is an issue, use
 * CRASPlaybackNumberOfVolumeSteps to overwrite it.
 */

static void configure_ucm_volume_settings(struct alsa_usb_output_node* output,
                                          struct alsa_usb_io* aio,
                                          bool software_volume_needed) {
  struct cras_ionode* node = &output->common.base;
  int number_of_volume_steps = -1;
  int rc = 0;

  node->software_volume_needed = software_volume_needed;
  syslog(LOG_INFO, "Use %s volume for %s with UCM.",
         node->software_volume_needed ? "software" : "hardware", node->name);
  const char* mixer_name =
      ucm_get_playback_mixer_elem_for_dev(aio->common.ucm, node->name);
  // In the UCM, if the PlaybackMixerElem is set then it should always use
  // HW volume because it has an associated control.
  CRAS_CHECK(!mixer_name || !software_volume_needed);

  node->number_of_volume_steps = NUMBER_OF_VOLUME_STEPS_DEFAULT;
  rc = ucm_get_playback_number_of_volume_steps_for_dev(
      aio->common.ucm, node->name, &number_of_volume_steps);
  if (!rc) {
    // number_of_volume_steps is used as a denominator to calculate percentage,
    // so it must be non-zero when set to node.
    CRAS_CHECK(number_of_volume_steps > 0);
  }
  // If the developer wants to tune volume steps, must use HW volume.
  CRAS_CHECK((number_of_volume_steps == -1) || !software_volume_needed);

  // You only need to configure the parameter if you're using a hardware volume.
  if (!software_volume_needed) {
    if (number_of_volume_steps != -1) {
      node->number_of_volume_steps = number_of_volume_steps;
    } else {
      node->number_of_volume_steps =
          MIN(cras_alsa_mixer_get_playback_step(output->common.mixer),
              NUMBER_OF_VOLUME_STEPS_DEFAULT);
    }
  }

  // number_of_volume_steps is used as a denominator to calculate percentage, so
  // it must be non-zero when set to node.
  CRAS_CHECK(node->number_of_volume_steps > 0);
}

/* Settle everything about volume on an output node. For eaxmple: SW or HW
 * volume to use, volume range check, volume curve construction.
 */
static void finalize_volume_settings(struct alsa_usb_output_node* output,
                                     struct alsa_usb_io* aio) {
  long max, min;
  const struct cras_volume_curve* curve;
  struct cras_ionode* node = &output->common.base;

  cras_alsa_mixer_get_playback_dBFS_range(aio->common.mixer,
                                          output->common.mixer, &max, &min);
  syslog(LOG_DEBUG, "%s's output volume range: [%ld %ld]", node->name, min,
         max);

  if (aio->common.ucm) {
    configure_ucm_volume_settings(output, aio,
                                  ucm_get_use_software_volume(aio->common.ucm));
  } else {
    configure_default_volume_settings(output, aio, min, max);
  }

  // Create volume curve for nodes base on cras config.
  output->volume_curve =
      usb_create_volume_curve_for_output(aio->common.config, output);
  /* if we finally decide to use HW volume and no volume curve in cras config,
   * create volume curve. */
  if (!output->volume_curve && !node->software_volume_needed &&
      !cras_system_get_using_default_volume_curve_for_usb_audio_device()) {
    output->volume_curve = cras_volume_curve_create_simple_step(0, max - min);
  }

  // Lastly, construct software volume scaler from the curve.
  curve = usb_get_curve_for_output_node(aio, output);
  node->softvol_scalers = softvol_build_from_curve(curve);
}

int cras_alsa_usb_iodev_legacy_complete_init(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  enum CRAS_STREAM_DIRECTION direction;
  int err;
  int is_first;
  struct cras_alsa_mixer* mixer;
  struct cras_ionode* node;

  if (!aio) {
    return -EINVAL;
  }
  direction = iodev->direction;
  is_first = aio->common.is_first;
  mixer = aio->common.mixer;

  /* Create output nodes for mixer controls, such as Headphone
   * and Speaker, only for the first device. */
  if (direction == CRAS_STREAM_OUTPUT && is_first) {
    cras_alsa_mixer_list_outputs(mixer, usb_new_output_by_mixer_control, aio);
  } else if (direction == CRAS_STREAM_INPUT && is_first) {
    cras_alsa_mixer_list_inputs(mixer, usb_new_input_by_mixer_control, aio);
  }

  err = cras_alsa_jack_list_find_jacks_by_name_matching(
      aio->common.jack_list,
      iodev->direction == CRAS_STREAM_OUTPUT
          ? add_output_node_and_associate_jack
          : add_input_node_and_associate_jack,
      aio);
  if (err) {
    return err;
  }

  /* Create nodes for jacks that aren't associated with an
   * already existing node. Get an initial read of the jacks for
   * this device. */
  cras_alsa_jack_list_report(aio->common.jack_list);

  /* Make a default node if there is still no node for this
   * device, or we still don't have the "Speaker"/"Internal Mic"
   * node for the first internal device. Note that the default
   * node creation can be suppressed by UCM flags for platforms
   * which really don't have an internal device. */
  if ((direction == CRAS_STREAM_OUTPUT) &&
      !usb_no_create_default_output_node(aio) && !aio->common.base.nodes) {
    usb_new_output(aio, NULL, DEFAULT);
  } else if ((direction == CRAS_STREAM_INPUT) &&
             !usb_no_create_default_input_node(aio) &&
             !aio->common.base.nodes) {
    usb_new_input(aio, NULL, DEFAULT);
  }

  // Build software volume scalers.
  if (direction == CRAS_STREAM_OUTPUT) {
    DL_FOREACH (iodev->nodes, node) {
      finalize_volume_settings((struct alsa_usb_output_node*)node, aio);
    }
  }

  // Set the active node as the best node we have now.
  usb_alsa_iodev_set_active_node(&aio->common.base,
                                 first_plugged_node(&aio->common.base), 0);

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
  struct alsa_common_node* anode = NULL;
  struct mixer_control* control;

  if (!aio || !section) {
    return -EINVAL;
  }

  /* Allow this section to add as a new node only if the device id
   * or dependent device id matches this iodev. */
  if (((uint32_t)section->dev_idx != aio->common.device_index) &&
      ((uint32_t)section->dependent_dev_idx != aio->common.device_index)) {
    return -EINVAL;
  }

  // Set flag has_dependent_dev for the case of dependent device.
  if (section->dependent_dev_idx != -1) {
    aio->common.has_dependent_dev = 1;
  }

  /* Check here in case the DmaPeriodMicrosecs flag has only been
   * specified on one of many device entries with the same PCM. */
  if (aio->common.ucm && !aio->common.dma_period_set_microsecs) {
    aio->common.dma_period_set_microsecs =
        ucm_get_dma_period_for_dev(aio->common.ucm, section->name);
  }

  /* Create a node matching this section. If there is a matching
   * control use that, otherwise make a node without a control. */
  control = cras_alsa_mixer_get_control_for_section(aio->common.mixer, section);
  const char* mixer_name = section->mixer_name;
  /*
    If the UCM specifies a mixer control for a node, but the ALSA mixer control
    is not found using the node name, suppress node creation and return an
    error.
  */
  if (mixer_name && !control) {
    syslog(LOG_ERR,
           "mixer name %s is specified in UCM, but ALSA mixer control is not "
           "found",
           mixer_name);
    return -EINVAL;
  }
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    struct alsa_usb_output_node* output_node =
        usb_new_output(aio, control, section->name);
    if (!output_node) {
      return -ENOMEM;
    }
    anode = &output_node->common;
  } else if (iodev->direction == CRAS_STREAM_INPUT) {
    struct alsa_usb_input_node* input_node =
        usb_new_input(aio, control, section->name);
    if (!input_node) {
      return -ENOMEM;
    }
    anode = &input_node->common;
  }
  if (!anode) {
    return -EINVAL;
  }

  // Find any jack controls for this device.
  return cras_alsa_jack_list_add_jack_for_section(
      aio->common.jack_list, section, (struct cras_alsa_jack**)&anode->jack);
}

void cras_alsa_usb_iodev_ucm_complete_init(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  struct cras_ionode* node;

  if (!iodev) {
    return;
  }

  // Get an initial read of the jacks for this device.
  cras_alsa_jack_list_report(aio->common.jack_list);

  // Build software volume scaler.
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    DL_FOREACH (iodev->nodes, node) {
      finalize_volume_settings((struct alsa_usb_output_node*)node, aio);
    }
  }

  // Set the active node as the best node we have now.
  usb_alsa_iodev_set_active_node(&aio->common.base,
                                 first_plugged_node(&aio->common.base), 0);

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
  int rc = cras_iodev_list_rm(iodev);

  if (rc == -EBUSY) {
    syslog(LOG_WARNING, "card type: %s, Failed to remove iodev %s",
           cras_card_type_to_string(aio->common.card_type), iodev->info.name);
    return;
  }

  // Free resources when device successfully removed.
  cras_alsa_jack_list_destroy(aio->common.jack_list);
  usb_free_alsa_iodev_resources(aio);
  cras_volume_curve_destroy(aio->common.default_volume_curve);
  free(iodev);
}

unsigned cras_alsa_usb_iodev_index(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  return aio->common.device_index;
}

int cras_alsa_usb_iodev_has_hctl_jacks(struct cras_iodev* iodev) {
  struct alsa_usb_io* aio = (struct alsa_usb_io*)iodev;
  return cras_alsa_jack_list_has_hctl_jacks(aio->common.jack_list);
}

static void usb_alsa_iodev_unmute_node(struct alsa_usb_io* aio,
                                       struct cras_ionode* ionode) {
  struct alsa_usb_output_node* active = (struct alsa_usb_output_node*)ionode;
  struct mixer_control* mixer = active->common.mixer;
  struct alsa_usb_output_node* output;
  struct cras_ionode* node;

  /* If this node is associated with mixer output, unmute the
   * active mixer output and mute all others, otherwise just set
   * the node as active and set the volume curve. */
  if (mixer) {
    // Unmute the active mixer output, mute all others.
    DL_FOREACH (aio->common.base.nodes, node) {
      output = (struct alsa_usb_output_node*)node;
      if (output->common.mixer) {
        cras_alsa_mixer_set_output_active_state(output->common.mixer,
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

  cras_alsa_common_set_active_node(iodev, ionode);
  cras_iodev_update_dsp(iodev);
skip:
  usb_enable_active_ucm(aio, dev_enabled);
  // Setting the volume will also unmute if the system isn't muted.
  usb_init_device_settings(aio);
  return 0;
}

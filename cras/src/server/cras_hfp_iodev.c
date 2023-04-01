/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_hfp_iodev.h"

#include <assert.h>
#include <stdbool.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>

#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_hfp_ag_profile.h"
#include "cras/src/server/cras_hfp_slc.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_sco.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_sr.h"
#include "cras/src/server/cras_sr_bt_util.h"
#include "cras/src/server/cras_system_state.h"
#include "cras_util.h"
#include "third_party/strlcpy/strlcpy.h"
#include "third_party/utlist/utlist.h"

// Implementation of bluetooth hands-free profile iodev.
struct hfp_io {
  // The cras_iodev structure base class.
  struct cras_iodev base;
  // The assciated bt_device.
  struct cras_bt_device* device;
  // Handle to the HFP service level connection.
  struct hfp_slc_handle* slc;
  // cras_sco taking care of SCO data read/write.
  struct cras_sco* sco;
  // Flag to indicate if valid samples are drained
  // in no stream state. Only used for output.
  bool drain_complete;
  // Number of zero data in frames have been filled
  // to buffer of cras_sco in no stream state. Only used for output
  unsigned int filled_zeros;
  // Indicates whether the cras_sr bt model is enabled.
  bool is_cras_sr_bt_enabled;
};

static size_t get_sample_rate(struct cras_iodev* iodev) {
  struct hfp_io* hfpio = (struct hfp_io*)iodev;
  if (iodev->direction == CRAS_STREAM_INPUT && hfpio->is_cras_sr_bt_enabled) {
    return 24000;
  }
  if (hfp_slc_get_selected_codec(hfpio->slc) == HFP_CODEC_ID_MSBC) {
    return 16000;
  }
  return 8000;
}

static int update_supported_formats(struct cras_iodev* iodev) {
  free(iodev->supported_rates);
  iodev->supported_rates = (size_t*)malloc(2 * sizeof(size_t));

  // 16 bit, mono, 8kHz for narrowband and 16KHz for wideband
  iodev->supported_rates[0] = get_sample_rate(iodev);
  iodev->supported_rates[1] = 0;

  free(iodev->supported_channel_counts);
  iodev->supported_channel_counts = (size_t*)malloc(2 * sizeof(size_t));
  if (!iodev->supported_channel_counts) {
    return -ENOMEM;
  }
  iodev->supported_channel_counts[0] = 1;
  iodev->supported_channel_counts[1] = 0;

  free(iodev->supported_formats);
  iodev->supported_formats =
      (snd_pcm_format_t*)malloc(2 * sizeof(snd_pcm_format_t));
  if (!iodev->supported_formats) {
    return -ENOMEM;
  }
  iodev->supported_formats[0] = SND_PCM_FORMAT_S16_LE;
  iodev->supported_formats[1] = 0;

  return 0;
}

static int no_stream(struct cras_iodev* iodev, int enable) {
  struct hfp_io* hfpio = (struct hfp_io*)iodev;
  struct timespec hw_tstamp;
  unsigned int hw_level;
  unsigned int level_target;

  if (iodev->direction != CRAS_STREAM_OUTPUT) {
    return 0;
  }

  hw_level = iodev->frames_queued(iodev, &hw_tstamp);
  if (enable) {
    if (!hfpio->drain_complete && (hw_level <= hfpio->filled_zeros)) {
      hfpio->drain_complete = 1;
    }
    hfpio->filled_zeros +=
        cras_sco_fill_output_with_zeros(hfpio->sco, iodev->buffer_size);
    return 0;
  }

  // Leave no stream state.
  level_target = iodev->min_cb_level;
  if (hfpio->drain_complete) {
    cras_sco_force_output_level(hfpio->sco, level_target);
  } else {
    unsigned int valid_samples = 0;
    if (hw_level > hfpio->filled_zeros) {
      valid_samples = hw_level - hfpio->filled_zeros;
    }
    level_target = MAX(level_target, valid_samples);

    if (level_target > hw_level) {
      cras_sco_fill_output_with_zeros(hfpio->sco, level_target - hw_level);
    } else {
      cras_sco_force_output_level(hfpio->sco, level_target);
    }
  }
  hfpio->drain_complete = 0;
  hfpio->filled_zeros = 0;

  return 0;
}

static int frames_queued(const struct cras_iodev* iodev,
                         struct timespec* tstamp) {
  struct hfp_io* hfpio = (struct hfp_io*)iodev;

  if (!cras_sco_running(hfpio->sco)) {
    return -EINVAL;
  }

  /* Do not enable timestamp mechanism on HFP device because last time
   * stamp might be a long time ago and it is not really useful. */
  clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
  return cras_sco_buf_queued(hfpio->sco, iodev->direction);
}

static int output_underrun(struct cras_iodev* iodev) {
  // Handle it the same way as cras_iodev_output_underrun().
  return cras_iodev_fill_odev_zeros(iodev, iodev->min_cb_level, true);
}

/* Handles cras sr bt enabling and disabling cases.
 *
 * Note that the device can be used, whether cras sr bt is enabled or not.
 * The result will be stored in the member variable `is_cras_sr_bt_enabled`
 *
 * Args:
 *    iodev: the hfp iodev.
 *    status: the result of cras_sr_bt_can_be_enabled().
 */
static void handle_cras_sr_bt_enable_disable(
    struct cras_iodev* iodev,
    const enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS status) {
  struct hfp_io* hfpio = (struct hfp_io*)iodev;

  if (iodev->direction == CRAS_STREAM_INPUT &&
      status == CRAS_SR_BT_CAN_BE_ENABLED_STATUS_OK) {
    int err = cras_sco_enable_cras_sr_bt(
        hfpio->sco, hfp_slc_get_selected_codec(hfpio->slc) == HFP_CODEC_ID_MSBC
                        ? SR_BT_WBS
                        : SR_BT_NBS);
    if (err < 0) {
      syslog(LOG_WARNING,
             "cras_sr is disabled due to "
             "cras_sco_enable_cras_sr_bt failed");
      hfpio->is_cras_sr_bt_enabled = false;
    } else {
      hfpio->is_cras_sr_bt_enabled = true;
    }
  } else {
    cras_sco_disable_cras_sr_bt(hfpio->sco);
    hfpio->is_cras_sr_bt_enabled = false;
  }
}

static inline void handle_cras_sr_bt_uma_log(
    struct cras_iodev* iodev,
    const enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS status) {
  if (iodev->direction != CRAS_STREAM_INPUT) {
    return;
  }

  struct hfp_io* hfpio = (struct hfp_io*)iodev;
  cras_sr_bt_send_uma_log(iodev, status, hfpio->is_cras_sr_bt_enabled);
}

/* Handles cras sr bt enabling and disabling cases and also uma logs.
 *
 * Args:
 *    iodev: the hfp iodev.
 */
static void handle_cras_sr_bt(struct cras_iodev* iodev) {
  const enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS status =
      cras_sr_bt_can_be_enabled();
  handle_cras_sr_bt_enable_disable(iodev, status);
  handle_cras_sr_bt_uma_log(iodev, status);
}

static int open_dev(struct cras_iodev* iodev) {
  struct hfp_io* hfpio = (struct hfp_io*)iodev;
  int sk, mtu, ret;
  int sco_handle;

  if (cras_sco_running(hfpio->sco)) {
    goto sco_running;
  }

  /*
   * Might require a codec negotiation before building the sco connection.
   */
  hfp_slc_codec_connection_setup(hfpio->slc);

  ret = cras_bt_device_sco_connect(
      hfpio->device, hfp_slc_get_selected_codec(hfpio->slc), false);
  if (ret < 0) {
    goto error;
  }

  sk = ret;
  ret = cras_sco_set_fd(hfpio->sco, ret);
  if (ret < 0) {
    syslog(LOG_WARNING, "Failed to set SCO fd(%d): %d", sk, ret);
  }

  mtu = cras_bt_device_sco_packet_size(hfpio->device, sk,
                                       hfp_slc_get_selected_codec(hfpio->slc));

  if (mtu < 0) {
    ret = mtu;
    goto error;
  }

  handle_cras_sr_bt(iodev);

  // Start cras_sco
  ret = cras_sco_start(mtu, hfp_slc_get_selected_codec(hfpio->slc), hfpio->sco);
  if (ret < 0) {
    goto error;
  }

  sco_handle = cras_bt_device_sco_handle(sk);
  cras_bt_device_report_hfp_start_stop_status(hfpio->device, true, sco_handle);

  hfpio->drain_complete = 0;
  hfpio->filled_zeros = 0;
sco_running:
  return 0;
error:
  syslog(LOG_WARNING, "Failed to open HFP iodev: %d", ret);
  return ret;
}

static int configure_dev(struct cras_iodev* iodev) {
  struct hfp_io* hfpio = (struct hfp_io*)iodev;
  int ret;

  // Assert format is set before opening device.
  if (iodev->format == NULL) {
    return -EINVAL;
  }

  iodev->format->format = SND_PCM_FORMAT_S16_LE;
  cras_iodev_init_audio_area(iodev, iodev->format->num_channels);

  ret = cras_sco_add_iodev(hfpio->sco, iodev->direction, iodev->format);
  if (ret < 0) {
    return ret;
  }
  ret = hfp_set_call_status(hfpio->slc, 1);
  if (ret < 0) {
    return ret;
  }
  iodev->buffer_size = cras_sco_buf_size(hfpio->sco, iodev->direction);

  return 0;
}

static int close_dev(struct cras_iodev* iodev) {
  struct hfp_io* hfpio = (struct hfp_io*)iodev;

  cras_sco_rm_iodev(hfpio->sco, iodev->direction);
  if (cras_sco_running(hfpio->sco) && !cras_sco_has_iodev(hfpio->sco)) {
    cras_sco_stop(hfpio->sco);
    cras_sco_close_fd(hfpio->sco);
    hfp_set_call_status(hfpio->slc, 0);

    cras_bt_device_report_hfp_start_stop_status(hfpio->device, false, 0);
  }

  cras_iodev_free_format(iodev);
  cras_iodev_free_audio_area(iodev);
  return 0;
}

static void set_hfp_volume(struct cras_iodev* iodev) {
  size_t volume;
  struct hfp_io* hfpio = (struct hfp_io*)iodev;

  volume = cras_system_get_volume();
  if (iodev->active_node) {
    volume = cras_iodev_adjust_node_volume(iodev->active_node, volume);
  }

  hfp_event_speaker_gain(hfpio->slc, volume);
}

static int delay_frames(const struct cras_iodev* iodev) {
  struct timespec tstamp;

  return frames_queued(iodev, &tstamp);
}

static int get_buffer(struct cras_iodev* iodev,
                      struct cras_audio_area** area,
                      unsigned* frames) {
  struct hfp_io* hfpio = (struct hfp_io*)iodev;
  uint8_t* dst = NULL;

  if (!cras_sco_running(hfpio->sco)) {
    return -EINVAL;
  }

  cras_sco_buf_acquire(hfpio->sco, iodev->direction, &dst, frames);

  iodev->area->frames = *frames;
  // HFP is mono only.
  iodev->area->channels[0].step_bytes = cras_get_format_bytes(iodev->format);
  iodev->area->channels[0].buf = dst;

  *area = iodev->area;
  return 0;
}

static int put_buffer(struct cras_iodev* iodev, unsigned nwritten) {
  struct hfp_io* hfpio = (struct hfp_io*)iodev;

  if (!cras_sco_running(hfpio->sco)) {
    return -EINVAL;
  }

  cras_sco_buf_release(hfpio->sco, iodev->direction, nwritten);
  return 0;
}

static int flush_buffer(struct cras_iodev* iodev) {
  struct hfp_io* hfpio = (struct hfp_io*)iodev;
  unsigned nframes;

  if (iodev->direction == CRAS_STREAM_INPUT) {
    nframes = cras_sco_buf_queued(hfpio->sco, iodev->direction);
    cras_sco_buf_release(hfpio->sco, iodev->direction, nframes);
  }
  return 0;
}

static void update_active_node(struct cras_iodev* iodev,
                               unsigned node_idx,
                               unsigned dev_enabled) {}

void hfp_free_resources(struct hfp_io* hfpio) {
  struct cras_ionode* node;
  node = hfpio->base.active_node;
  if (node) {
    cras_iodev_rm_node(&hfpio->base, node);
    free(node);
  }
  free(hfpio->base.supported_channel_counts);
  free(hfpio->base.supported_rates);
  free(hfpio->base.supported_formats);
  cras_iodev_free_resources(&hfpio->base);
}

struct cras_iodev* hfp_iodev_create(enum CRAS_STREAM_DIRECTION dir,
                                    struct cras_bt_device* device,
                                    struct hfp_slc_handle* slc,
                                    struct cras_sco* sco) {
  struct hfp_io* hfpio;
  struct cras_iodev* iodev;
  struct cras_ionode* node;
  const char* name;

  hfpio = (struct hfp_io*)calloc(1, sizeof(*hfpio));
  if (!hfpio) {
    goto error;
  }

  iodev = &hfpio->base;
  iodev->direction = dir;

  hfpio->device = device;
  hfpio->slc = slc;

  // Set iodev's name to device readable name or the address.
  name = cras_bt_device_name(device);
  if (!name) {
    name = cras_bt_device_object_path(device);
  }

  snprintf(iodev->info.name, sizeof(iodev->info.name), "%s", name);
  iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = 0;
  iodev->info.stable_id = cras_bt_device_get_stable_id(device);

  iodev->configure_dev = configure_dev;
  iodev->frames_queued = frames_queued;
  iodev->delay_frames = delay_frames;
  iodev->get_buffer = get_buffer;
  iodev->put_buffer = put_buffer;
  iodev->flush_buffer = flush_buffer;
  iodev->no_stream = no_stream;
  iodev->open_dev = open_dev;
  iodev->close_dev = close_dev;
  iodev->update_supported_formats = update_supported_formats;
  iodev->update_active_node = update_active_node;
  iodev->output_underrun = output_underrun;
  // Assign set_volume callback only for output iodev.
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    iodev->set_volume = set_hfp_volume;
  }

  node = (struct cras_ionode*)calloc(1, sizeof(*node));
  if (!node) {
    goto error;
  }
  node->dev = iodev;
  strlcpy(node->name, iodev->info.name, sizeof(node->name));

  node->plugged = 1;
  /* If headset mic doesn't support the wideband speech, report a
   * different node type so UI can set different plug priority. */
  node->type = CRAS_NODE_TYPE_BLUETOOTH;
  if (!hfp_slc_get_wideband_speech_supported(hfpio->slc) &&
      (dir == CRAS_STREAM_INPUT)) {
    node->type = CRAS_NODE_TYPE_BLUETOOTH_NB_MIC;
  }

  node->volume = 100;
  gettimeofday(&node->plugged_time, NULL);

  node->btflags |= CRAS_BT_FLAG_HFP;

  /* Prepare active node before append, so bt_io can extract correct
   * info from HFP iodev and node. */
  cras_iodev_add_node(iodev, node);
  cras_iodev_set_active_node(iodev, node);
  cras_bt_device_append_iodev(device, iodev, CRAS_BT_FLAG_HFP);

  hfpio->sco = sco;

  // Record max supported channels into cras_iodev_info.
  iodev->info.max_supported_channels = 1;

  ewma_power_disable(&iodev->ewma);

  return iodev;

error:
  if (hfpio) {
    hfp_free_resources(hfpio);
    free(hfpio);
  }
  return NULL;
}

void hfp_iodev_destroy(struct cras_iodev* iodev) {
  struct hfp_io* hfpio = (struct hfp_io*)iodev;

  cras_bt_device_rm_iodev(hfpio->device, iodev);
  hfp_free_resources(hfpio);
  free(hfpio);
}

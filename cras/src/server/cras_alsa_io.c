/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/src/server/cras_alsa_io.h"

#include <alsa/asoundlib.h>
#include <alsa/use-case.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "cras/common/check.h"
#include "cras/src/common/cras_alsa_card_info.h"
#include "cras/src/common/cras_log.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/config/cras_card_config.h"
#include "cras/src/server/cras_alsa_common_io.h"
#include "cras/src/server/cras_alsa_helpers.h"
#include "cras/src/server/cras_alsa_jack.h"
#include "cras/src/server/cras_alsa_mixer.h"
#include "cras/src/server/cras_alsa_ucm.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_dsp.h"
#include "cras/src/server/cras_dsp_offload.h"
#include "cras/src/server/cras_hotword_handler.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_ramp.h"
#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_utf8.h"
#include "cras/src/server/cras_volume_curve.h"
#include "cras/src/server/dev_stream.h"
#include "cras/src/server/softvol_curve.h"
#include "cras_audio_format.h"
#include "cras_iodev_info.h"
#include "cras_system_state.h"
#include "cras_types.h"
#include "cras_util.h"
#include "third_party/strlcpy/strlcpy.h"
#include "third_party/superfasthash/sfh.h"
#include "third_party/utlist/utlist.h"

// Bluetooth SCO offload node name for input/output
#define SCO_LINE_IN "SCO Line In"
#define SCO_LINE_OUT "SCO Line Out"

/*
 * This extends cras_ionode to include alsa-specific information.
 */
struct alsa_output_node {
  struct alsa_common_node common;
  // Volume curve for this node.
  struct cras_volume_curve* volume_curve;
};

struct alsa_input_node {
  struct alsa_common_node common;
};

struct alsa_io_group;

/*
 * Child of cras_iodev, alsa_io handles ALSA interaction for sound devices.
 */
struct alsa_io {
  // The alsa_io_common structure "base class".
  struct alsa_common_io common;
  // If echo cancellation is enabled in DSP
  bool dsp_echo_cancellation_enabled;
  // If noise suppression is enabled in DSP
  bool dsp_noise_suppression_enabled;
  // If gain control is enabled in DSP
  bool dsp_gain_control_enabled;
  // Use case of this device according to UCM.
  enum CRAS_USE_CASE use_case;
  // Pointer to the shared group info. NULL if the device is not in a group.
  // alsa_io_group is ref-counted and owned by its member devices.
  struct alsa_io_group* group;
};

struct alsa_io_group {
  struct alsa_io* devs[CRAS_NUM_USE_CASES];
  size_t num_devs;
  // The device that owns and manages nodes in the group.
  struct alsa_io* nodes_owner;
};

static void init_device_settings(struct alsa_io* aio);

static int alsa_iodev_set_active_node(struct cras_iodev* iodev,
                                      struct cras_ionode* ionode,
                                      unsigned dev_enabled);

static int update_supported_formats(struct cras_iodev* iodev);

/*
 * Defines the default values of nodes.
 */
static const struct {
  const char* name;
  enum CRAS_NODE_TYPE type;
  enum CRAS_NODE_POSITION position;
} node_defaults[] = {
    {
        .name = DEFAULT,
        .type = CRAS_NODE_TYPE_UNKNOWN,
        .position = NODE_POSITION_INTERNAL,
    },
    {
        .name = INTERNAL_SPEAKER,
        .type = CRAS_NODE_TYPE_INTERNAL_SPEAKER,
        .position = NODE_POSITION_INTERNAL,
    },
    {
        .name = INTERNAL_MICROPHONE,
        .type = CRAS_NODE_TYPE_MIC,
        .position = NODE_POSITION_INTERNAL,
    },
    {
        .name = KEYBOARD_MIC,
        .type = CRAS_NODE_TYPE_MIC,
        .position = NODE_POSITION_KEYBOARD,
    },
    {
        .name = HDMI,
        .type = CRAS_NODE_TYPE_HDMI,
        .position = NODE_POSITION_EXTERNAL,
    },
    {
        .name = "IEC958",
        .type = CRAS_NODE_TYPE_HDMI,
        .position = NODE_POSITION_EXTERNAL,
    },
    {
        .name = "Headphone",
        .type = CRAS_NODE_TYPE_HEADPHONE,
        .position = NODE_POSITION_EXTERNAL,
    },
    {
        .name = "Front Headphone",
        .type = CRAS_NODE_TYPE_HEADPHONE,
        .position = NODE_POSITION_EXTERNAL,
    },
    {
        .name = "Front Mic",
        .type = CRAS_NODE_TYPE_MIC,
        .position = NODE_POSITION_FRONT,
    },
    {
        .name = "Rear Mic",
        .type = CRAS_NODE_TYPE_MIC,
        .position = NODE_POSITION_REAR,
    },
    {
        .name = "Mic",
        .type = CRAS_NODE_TYPE_MIC,
        .position = NODE_POSITION_EXTERNAL,
    },
    {
        .name = HOTWORD_DEV,
        .type = CRAS_NODE_TYPE_HOTWORD,
        .position = NODE_POSITION_INTERNAL,
    },
    {
        .name = "Haptic",
        .type = CRAS_NODE_TYPE_HAPTIC,
        .position = NODE_POSITION_INTERNAL,
    },
    {
        .name = "Rumbler",
        .type = CRAS_NODE_TYPE_HAPTIC,
        .position = NODE_POSITION_INTERNAL,
    },
    {
        .name = "Line Out",
        .type = CRAS_NODE_TYPE_LINEOUT,
        .position = NODE_POSITION_EXTERNAL,
    },
    {
        .name = SCO_LINE_IN,
        .type = CRAS_NODE_TYPE_BLUETOOTH,
        .position = NODE_POSITION_EXTERNAL,
    },
    {
        .name = SCO_LINE_OUT,
        .type = CRAS_NODE_TYPE_BLUETOOTH,
        .position = NODE_POSITION_EXTERNAL,
    },
    {
        .name = "Echo Reference",
        .type = CRAS_NODE_TYPE_ECHO_REFERENCE,
        .position = NODE_POSITION_INTERNAL,
    },
    {
        .name = LOOPBACK_CAPTURE,
        .type = CRAS_NODE_TYPE_ALSA_LOOPBACK,
        .position = NODE_POSITION_INTERNAL,
    },
    {
        .name = LOOPBACK_PLAYBACK,
        .type = CRAS_NODE_TYPE_ALSA_LOOPBACK,
        .position = NODE_POSITION_INTERNAL,
    },
};

static inline int set_hwparams(struct cras_iodev* iodev) {
  // If it's a wake on voice device, period_wakeups are required.
  int period_wakeup = (iodev->active_node->type == CRAS_NODE_TYPE_HOTWORD);
  return cras_alsa_common_set_hwparams(iodev, period_wakeup);
}

/*
 * iodev callbacks.
 */

static inline int frames_queued(const struct cras_iodev* iodev,
                                struct timespec* tstamp) {
  return cras_alsa_common_frames_queued(iodev, tstamp);
}

static inline int delay_frames(const struct cras_iodev* iodev) {
  return cras_alsa_common_delay_frames(iodev);
}

static inline int close_dev(struct cras_iodev* iodev) {
  return cras_alsa_common_close_dev(iodev);
}

static int empty_hotword_cb(void* arg, int revents) {
  // Only need this once.
  struct alsa_io* aio = (struct alsa_io*)arg;
  audio_thread_rm_callback(aio->common.poll_fd);
  aio->common.poll_fd = -1;
  aio->common.base.input_streaming = 1;

  // Send hotword triggered signal.
  return cras_hotword_send_triggered_msg();
}

static int open_dev(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  const char* pcm_name = NULL;

  aio->common.poll_fd = -1;
  /* For DependentPCM usage in HiFi.conf only.
   * Normally the pcm name should come from the alsa_io device, not from nodes.
   */
  if (aio->common.base.nodes) {
    struct alsa_common_node* anode =
        (struct alsa_common_node*)aio->common.base.active_node;
    pcm_name = anode->pcm_name;
  }
  return cras_alsa_common_open_dev(iodev, pcm_name);
}

static void init_quad_rotation_dsp_env_for_internal_speaker(
    struct cras_iodev* iodev) {
  if (iodev->active_node &&
      iodev->active_node->type == CRAS_NODE_TYPE_INTERNAL_SPEAKER &&
      iodev->format && iodev->format->num_channels == 4) {
    cras_dsp_set_variable_integer(iodev->dsp_context, "display_rotation",
                                  cras_system_get_display_rotation());
    cras_dsp_set_variable_integer(iodev->dsp_context, "FL",
                                  iodev->format->channel_layout[CRAS_CH_FL]);
    cras_dsp_set_variable_integer(iodev->dsp_context, "FR",
                                  iodev->format->channel_layout[CRAS_CH_FR]);
    cras_dsp_set_variable_integer(iodev->dsp_context, "RL",
                                  iodev->format->channel_layout[CRAS_CH_RL]);
    cras_dsp_set_variable_integer(iodev->dsp_context, "RR",
                                  iodev->format->channel_layout[CRAS_CH_RR]);

    cras_iodev_update_dsp(iodev);
  }
}

static int configure_dev(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
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

  syslog(LOG_DEBUG, "Configure alsa device %s rate %zuHz, %zu channels",
         aio->common.pcm_name, iodev->format->frame_rate,
         iodev->format->num_channels);

  rc = set_hwparams(iodev);
  if (rc < 0) {
    return rc;
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
    return rc;
  }

  // Initialize the dsp context for quad rotation plugin
  init_quad_rotation_dsp_env_for_internal_speaker(iodev);

  // Configure software params.
  rc = cras_alsa_set_swparams(aio->common.handle);
  if (rc < 0) {
    return rc;
  }

  // Initialize device settings.
  init_device_settings(aio);

  if (iodev->active_node->type == CRAS_NODE_TYPE_HOTWORD) {
    struct pollfd* ufds;
    int count, i;

    count = snd_pcm_poll_descriptors_count(aio->common.handle);
    if (count <= 0) {
      syslog(LOG_WARNING, "Invalid poll descriptors count\n");
      return count;
    }

    ufds = (struct pollfd*)malloc(sizeof(*ufds) * count);
    if (ufds == NULL) {
      return -ENOMEM;
    }

    rc = snd_pcm_poll_descriptors(aio->common.handle, ufds, count);
    if (rc < 0) {
      syslog(LOG_WARNING, "Getting hotword poll descriptors: %s\n",
             snd_strerror(rc));
      free(ufds);
      return rc;
    }

    for (i = 0; i < count; i++) {
      if (ufds[i].events & POLLIN) {
        aio->common.poll_fd = ufds[i].fd;
        break;
      }
    }
    free(ufds);

    if (aio->common.poll_fd >= 0) {
      audio_thread_add_events_callback(aio->common.poll_fd, empty_hotword_cb,
                                       aio, POLLIN);
    }
  }

  // Capture starts right away, playback will wait for samples.
  if (aio->common.alsa_stream == SND_PCM_STREAM_CAPTURE) {
    rc = cras_alsa_pcm_start(aio->common.handle);
    if (rc < 0) {
      syslog(LOG_ERR, "PCM %s Failed to start, ret: %s\n", aio->common.pcm_name,
             snd_strerror(rc));
    }
  }

  return 0;
}

/*
 * Check if ALSA device is opened by checking if handle is valid.
 * Note that to fully open a cras_iodev, ALSA device is opened first, then there
 * are some device init settings to be done in init_device_settings.
 * Therefore, when setting volume/mute/gain in init_device_settings,
 * cras_iodev is not in CRAS_IODEV_STATE_OPEN yet. We need to check if handle
 * is valid when setting those properties, instead of checking
 * cras_iodev_is_open.
 */
static int has_handle(const struct alsa_io* aio) {
  return !!aio->common.handle;
}

static int start(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  snd_pcm_t* handle = aio->common.handle;
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

static int get_buffer(struct cras_iodev* iodev,
                      struct cras_audio_area** area,
                      unsigned* frames) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
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
      if (!aio->common.sample_buf) {
        syslog(LOG_WARNING, "sample_buf is NULL");
        return -EINVAL;
      }
      memcpy(aio->common.sample_buf + iodev->input_dsp_offset * format_bytes,
             aio->common.mmap_buf + iodev->input_dsp_offset * format_bytes,
             (nframes - iodev->input_dsp_offset) * format_bytes);
    }
  }

  *area = iodev->area;
  *frames = nframes;

  return rc;
}

static int put_buffer(struct cras_iodev* iodev, unsigned nwritten) {
  struct alsa_io* aio = (struct alsa_io*)iodev;

  size_t format_bytes = cras_get_format_bytes(iodev->format);
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    if (!aio->common.sample_buf) {
      syslog(LOG_WARNING, "sample_buf is NULL");
      return -EINVAL;
    }
    if (!aio->common.mmap_buf) {
      syslog(LOG_WARNING, "mmap_buf is NULL");
      return -EINVAL;
    }
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

static int flush_buffer(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  snd_pcm_uframes_t nframes;

  if (iodev->direction == CRAS_STREAM_INPUT) {
    nframes = snd_pcm_avail(aio->common.handle);
    nframes = snd_pcm_forwardable(aio->common.handle);
    return snd_pcm_forward(aio->common.handle, nframes);
  }
  return 0;
}

static void update_active_node(struct cras_iodev* iodev,
                               unsigned node_idx,
                               unsigned dev_enabled) {
  struct cras_ionode* n;
  struct alsa_io* aio = (struct alsa_io*)iodev;

  // Find the iodev that manages nodes for the group.
  if (aio->group && aio->group->nodes_owner) {
    iodev = &aio->group->nodes_owner->common.base;
  }

  // If a node exists for node_idx, set it as active.
  DL_FOREACH (iodev->nodes, n) {
    if (n->idx == node_idx) {
      alsa_iodev_set_active_node(iodev, n, dev_enabled);
      return;
    }
  }

  alsa_iodev_set_active_node(iodev, first_plugged_node(iodev), dev_enabled);
}

static int update_channel_layout(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  int err = 0;

  /* If the capture channel map is specified in UCM, prefer it over
   * what ALSA provides. */
  if (aio->common.ucm) {
    struct alsa_common_node* node =
        (struct alsa_common_node*)iodev->active_node;
    if (node->channel_layout) {
      memcpy(iodev->format->channel_layout, node->channel_layout,
             CRAS_CH_MAX * sizeof(*node->channel_layout));

      /* Channel map might contain value higher than or equal to num_channels.
       * If that's the case, try to open the device using higher channel
       * count. */
      int min_valid_channels =
          cras_audio_format_get_least_num_channels(iodev->format);
      if (iodev->format->num_channels < min_valid_channels) {
        /* Use the minimum supported channel count that is still at least
         * min_valid_channels, or else return error to fall back to default
         * channel layout. */
        int target_channels = -1;
        for (int i = 0; iodev->supported_channel_counts[i] != 0; i++) {
          if (iodev->supported_channel_counts[i] >= min_valid_channels &&
              (target_channels == -1 ||
               iodev->supported_channel_counts[i] < target_channels)) {
            target_channels = iodev->supported_channel_counts[i];
          }
        }
        if (target_channels != -1) {
          syslog(LOG_WARNING,
                 "ALSA dev=%s UCM capture channel map exceeds requested "
                 "num_channels. min_valid_channels=%d, num_channels=%zu. "
                 "Using a higher channel count=%d",
                 aio->common.pcm_name, min_valid_channels,
                 iodev->format->num_channels, target_channels);
          iodev->format->num_channels = target_channels;
          return 0;
        }

        FRALOG(ALSAUCMCaptureChannelMapExceedsNumChannels,
               {"device", aio->common.pcm_name},
               {"min_valid_channels", tlsprintf("%d", min_valid_channels)},
               {"num_channels", tlsprintf("%d", iodev->format->num_channels)});

        syslog(LOG_ERR,
               "ALSA dev=%s UCM capture channel map exceeds requested "
               "num_channels. min_valid_channels=%d, num_channels=%zu. Falling "
               "back to default channel layout",
               aio->common.pcm_name, min_valid_channels,
               iodev->format->num_channels);

        return -EINVAL;
      }
      return 0;
    }
  }

  err = set_hwparams(iodev);
  if (err < 0) {
    return err;
  }

  return cras_alsa_get_channel_map(aio->common.handle, iodev->format);
}

static int set_hotword_model(struct cras_iodev* iodev, const char* model_name) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  if (!aio->common.ucm) {
    return -EINVAL;
  }

  return ucm_set_hotword_model(aio->common.ucm, model_name);
}

static char* get_hotword_models(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  if (!aio->common.ucm) {
    return NULL;
  }

  return ucm_get_hotword_models(aio->common.ucm);
}

/*
 * Alsa helper functions.
 */

static struct alsa_output_node* get_active_output(const struct alsa_io* aio) {
  return (struct alsa_output_node*)aio->common.base.active_node;
}

static struct alsa_input_node* get_active_input(const struct alsa_io* aio) {
  return (struct alsa_input_node*)aio->common.base.active_node;
}

/*
 * Gets the curve for the active output node. If the node doesn't have volume
 * curve specified, return the default volume curve of the common iodev.
 */
static const struct cras_volume_curve* get_curve_for_output_node(
    const struct alsa_io* aio,
    const struct alsa_output_node* node) {
  if (node && node->volume_curve) {
    return node->volume_curve;
  }
  return aio->common.default_volume_curve;
}

/*
 * Gets the curve for the active output.
 */
static const struct cras_volume_curve* get_curve_for_active_output(
    const struct alsa_io* aio) {
  struct alsa_output_node* node = get_active_output(aio);
  return get_curve_for_output_node(aio, node);
}

/*
 * Informs the system of the volume limits for this device.
 */
static void set_alsa_volume_limits(struct alsa_io* aio) {
  const struct cras_volume_curve* curve;

  // Only set the limits if the dev is active.
  if (!has_handle(aio)) {
    return;
  }

  curve = get_curve_for_active_output(aio);
  cras_system_set_volume_limits(curve->get_dBFS(curve, 1),  // min
                                curve->get_dBFS(curve, CRAS_MAX_SYSTEM_VOLUME));
}

/*
 * Sets the volume of the playback device to the specified level. Receives a
 * volume index from the system settings, ranging from 0 to 100, converts it to
 * dB using the volume curve, and sends the dB value to alsa.
 */
static void set_alsa_volume(struct cras_iodev* iodev) {
  const struct alsa_io* aio = (const struct alsa_io*)iodev;
  const struct cras_volume_curve* curve;
  size_t volume;
  struct alsa_output_node* aout;

  CRAS_CHECK(aio);
  if (aio->common.mixer == NULL) {
    return;
  }

  volume = cras_system_get_volume();
  curve = get_curve_for_active_output(aio);
  if (curve == NULL) {
    return;
  }
  aout = get_active_output(aio);
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
static void set_alsa_mute(struct cras_iodev* iodev) {
  const struct alsa_io* aio = (const struct alsa_io*)iodev;
  struct alsa_output_node* aout;

  if (!has_handle(aio)) {
    return;
  }

  aout = get_active_output(aio);
  cras_alsa_mixer_set_mute(aio->common.mixer, cras_system_get_mute(),
                           aout ? aout->common.mixer : NULL);
}

/*
 * Sets the capture gain according to the current active node's
 * |internal_capture_gain| in dBFS. Multiple nodes could share common
 * mixer controls so this needs to be called every time when active
 * node changes.
 */
static void set_alsa_capture_gain(struct cras_iodev* iodev) {
  const struct alsa_io* aio = (const struct alsa_io*)iodev;
  struct alsa_input_node* ain;
  long min_capture_gain, max_capture_gain, gain;
  CRAS_CHECK(aio);
  if (aio->common.mixer == NULL) {
    return;
  }

  // Only set the volume if the dev is active.
  if (!has_handle(aio)) {
    return;
  }

  ain = get_active_input(aio);

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
static int set_alsa_node_swapped(struct cras_iodev* iodev,
                                 struct cras_ionode* node,
                                 int enable) {
  const struct alsa_io* aio = (const struct alsa_io*)iodev;
  const struct alsa_common_node* anode = (const struct alsa_common_node*)node;
  CRAS_CHECK(aio);
  return ucm_enable_swap_mode(aio->common.ucm, anode->ucm_name, enable);
}

/*
 * Initializes the device settings according to system volume, mute, and
 * nodes' gain settings.
 */
static void init_device_settings(struct alsa_io* aio) {
  /* Register for volume/mute callback and set initial volume/mute for
   * the device. */
  if (aio->common.base.direction == CRAS_STREAM_OUTPUT) {
    set_alsa_volume_limits(aio);
    set_alsa_volume(&aio->common.base);
    set_alsa_mute(&aio->common.base);
  } else {
    set_alsa_capture_gain(&aio->common.base);
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
static void free_alsa_iodev_resources(struct alsa_io* aio) {
  struct cras_ionode* node;
  struct alsa_output_node* aout;

  free(aio->common.base.supported_rates);
  free(aio->common.base.supported_channel_counts);
  free(aio->common.base.supported_formats);

  DL_FOREACH (aio->common.base.nodes, node) {
    if (aio->common.base.direction == CRAS_STREAM_OUTPUT) {
      aout = (struct alsa_output_node*)node;
      cras_volume_curve_destroy(aout->volume_curve);
    }
    struct alsa_common_node* anode = (struct alsa_common_node*)node;
    free((void*)anode->pcm_name);
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
 * Returns true if this is the first internal device.
 */
static int first_internal_device(struct alsa_io* aio) {
  return aio->common.is_first;
}

/*
 * Returns true if there is already a node created with the given name.
 */
static int has_node(struct alsa_io* aio, const char* name) {
  struct cras_ionode* node;

  DL_FOREACH (aio->common.base.nodes, node) {
    if (!strcmp(node->name, name)) {
      return 1;
    }
  }

  return 0;
}

/*
 * Returns true if string s ends with the given suffix.
 */
int endswith(const char* s, const char* suffix) {
  size_t n = strlen(s);
  size_t m = strlen(suffix);
  return n >= m && !strcmp(s + (n - m), suffix);
}

/*
 * Drop the node name and replace it with node type.
 */
static void drop_node_name(struct cras_ionode* node) {
  if (node->type == CRAS_NODE_TYPE_HDMI) {
    strlcpy(node->name, HDMI, sizeof(node->name));
  } else {
    // Only HDMI node might have invalid name to drop
    syslog(LOG_ERR,
           "Unexpectedly drop node name for "
           "node: %s, type: %d",
           node->name, node->type);
    strlcpy(node->name, DEFAULT, sizeof(node->name));
  }
}

/*
 * Sets the initial plugged state and type of a node based on its
 * name. Chrome will assign priority to nodes base on node type.
 */
static void set_node_initial_state(struct cras_ionode* node,
                                   enum CRAS_ALSA_CARD_TYPE card_type) {
  unsigned i;

  node->volume = 100;
  node->type = CRAS_NODE_TYPE_UNKNOWN;
  // Go through the known names
  for (i = 0; i < ARRAY_SIZE(node_defaults); i++) {
    if (!strncmp(node->name, node_defaults[i].name,
                 strlen(node_defaults[i].name))) {
      node->position = node_defaults[i].position;
      node->plugged = (node->position != NODE_POSITION_EXTERNAL);
      node->type = node_defaults[i].type;
      if (node->plugged) {
        gettimeofday(&node->plugged_time, NULL);
      }
      break;
    }
  }

  /*
   * If we didn't find a matching name above, but the node is a jack node,
   * and there is no "HDMI" in the node name, then this must be a 3.5mm
   * headphone/mic.
   * Set its type and name to headphone/mic. The name is important because
   * it associates the UCM section to the node so the properties like
   * default node gain can be obtained.
   * This matches node names like "DAISY-I2S Mic Jack".
   * If HDMI is in the node name, set its type to HDMI. This matches node names
   * like "Rockchip HDMI Jack".
   */
  if (i == ARRAY_SIZE(node_defaults)) {
    if (endswith(node->name, "Jack") && !strstr(node->name, HDMI)) {
      if (node->dev->direction == CRAS_STREAM_OUTPUT) {
        node->type = CRAS_NODE_TYPE_HEADPHONE;
        strlcpy(node->name, HEADPHONE, sizeof(node->name));
      } else {
        node->type = CRAS_NODE_TYPE_MIC;
        strlcpy(node->name, MIC, sizeof(node->name));
      }
    }
    if (strstr(node->name, HDMI) &&
        node->dev->direction == CRAS_STREAM_OUTPUT) {
      node->type = CRAS_NODE_TYPE_HDMI;
    }
  }

  if (!is_utf8_string(node->name)) {
    drop_node_name(node);
  }

  /* Modifiers associate with device names through their suffixes.
   * Override echo refs accordingly as they match incorrectly on
   * their prefix.
   */
  if (endswith(node->name, SND_USE_CASE_MOD_ECHO_REF)) {
    node->type = CRAS_NODE_TYPE_ECHO_REFERENCE;
  }

  // TODO(b/289173343): Remove when output latency offset is moved out of
  // board.ini
  // Set output latency offset.
  if (node->dev->direction == CRAS_STREAM_OUTPUT &&
      node->type == CRAS_NODE_TYPE_INTERNAL_SPEAKER) {
    node->latency_offset_ms =
        cras_system_get_speaker_output_latency_offset_ms();
  }
}

static int get_ucm_flag_integer(struct alsa_io* aio,
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

static int auto_unplug_input_node(struct alsa_io* aio) {
  int result;
  if (get_ucm_flag_integer(aio, "AutoUnplugInputNode", &result)) {
    return 0;
  }
  return result;
}

static int auto_unplug_output_node(struct alsa_io* aio) {
  int result;
  if (get_ucm_flag_integer(aio, "AutoUnplugOutputNode", &result)) {
    return 0;
  }
  return result;
}

static int no_create_default_input_node(struct alsa_io* aio) {
  int result;
  if (get_ucm_flag_integer(aio, "NoCreateDefaultInputNode", &result)) {
    return 0;
  }
  return result;
}

static int no_create_default_output_node(struct alsa_io* aio) {
  int result;
  if (get_ucm_flag_integer(aio, "NoCreateDefaultOutputNode", &result)) {
    return 0;
  }
  return result;
}

static void set_input_default_node_gain(struct alsa_input_node* input,
                                        struct alsa_io* aio) {
  long gain;
  struct cras_ionode* node = (struct cras_ionode*)&input->common.base;

  node->internal_capture_gain = DEFAULT_CAPTURE_GAIN;
  node->ui_gain_scaler = 1.0f;

  if (!aio->common.ucm) {
    return;
  }

  if (ucm_get_default_node_gain(aio->common.ucm, input->common.ucm_name,
                                &gain) == 0) {
    node->internal_capture_gain = gain;
  }
}

static void set_input_node_intrinsic_sensitivity(struct alsa_input_node* input,
                                                 struct alsa_io* aio) {
  struct cras_ionode* node = (struct cras_ionode*)&input->common.base;
  long sensitivity;
  int rc;

  node->intrinsic_sensitivity = 0;

  if (!aio->common.ucm) {
    return;
  }
  rc = ucm_get_intrinsic_sensitivity(aio->common.ucm, input->common.ucm_name,
                                     &sensitivity);
  if (rc) {
    return;
  }

  node->intrinsic_sensitivity = sensitivity;
  node->internal_capture_gain = DEFAULT_CAPTURE_VOLUME_DBFS - sensitivity;
  syslog(LOG_DEBUG,
         "Use software gain %ld for %s because IntrinsicSensitivity %ld is"
         " specified in UCM",
         node->internal_capture_gain, node->name, sensitivity);
}

static void check_auto_unplug_output_node(struct alsa_io* aio,
                                          struct cras_ionode* node,
                                          int plugged) {
  struct cras_ionode* tmp;

  if (!auto_unplug_output_node(aio)) {
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
static struct alsa_output_node* new_output(struct alsa_io* aio,
                                           struct mixer_control* cras_control,
                                           const char* name) {
  CRAS_CHECK(name);
  int err;

  syslog(LOG_DEBUG, "New output node for '%s'", name);
  if (aio == NULL) {
    syslog(LOG_ERR, "Invalid aio when listing outputs.");
    return NULL;
  }
  struct alsa_output_node* output =
      (struct alsa_output_node*)calloc(1, sizeof(*output));
  struct cras_ionode* node = (struct cras_ionode*)&output->common.base;
  if (output == NULL) {
    syslog(LOG_ERR, "Out of memory when listing outputs.");
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

  if (str_equals(name, SCO_LINE_OUT)) {
    node->btflags |= CRAS_BT_FLAG_SCO_OFFLOAD;
  }
  output->common.mixer = cras_control;

  strlcpy(node->name, name, sizeof(node->name));
  strlcpy(output->common.ucm_name, name, sizeof(output->common.ucm_name));
  set_node_initial_state(node, aio->common.card_type);
  cras_iodev_add_node(&aio->common.base, node);
  check_auto_unplug_output_node(aio, node, node->plugged);
  return output;
}

static void new_output_by_mixer_control(struct mixer_control* cras_output,
                                        void* callback_arg) {
  struct alsa_io* aio = (struct alsa_io*)callback_arg;
  const char* ctl_name;

  ctl_name = cras_alsa_mixer_get_control_name(cras_output);
  if (!ctl_name) {
    return;
  }
  new_output(aio, cras_output, ctl_name);
}

static void check_auto_unplug_input_node(struct alsa_io* aio,
                                         struct cras_ionode* node,
                                         int plugged) {
  struct cras_ionode* tmp;
  if (!auto_unplug_input_node(aio)) {
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

static struct alsa_input_node* new_input(struct alsa_io* aio,
                                         struct mixer_control* cras_input,
                                         const char* name) {
  struct cras_iodev* iodev = &aio->common.base;
  int err;

  struct alsa_input_node* input =
      (struct alsa_input_node*)calloc(1, sizeof(*input));
  if (input == NULL) {
    syslog(LOG_ERR, "Out of memory when listing inputs.");
    return NULL;
  }
  struct cras_ionode* node = (struct cras_ionode*)&input->common.base;

  node->dev = &aio->common.base;
  node->idx = aio->common.next_ionode_index++;
  node->stable_id =
      SuperFastHash(name, strlen(name), aio->common.base.info.stable_id);
  if (str_equals(name, SCO_LINE_IN)) {
    node->btflags |= CRAS_BT_FLAG_SCO_OFFLOAD;
  }
  input->common.mixer = cras_input;
  strlcpy(node->name, name, sizeof(node->name));
  strlcpy(input->common.ucm_name, name, sizeof(input->common.ucm_name));
  set_node_initial_state(node, aio->common.card_type);
  set_input_default_node_gain(input, aio);
  set_input_node_intrinsic_sensitivity(input, aio);

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

  // Set NC providers.
  node->nc_providers = cras_alsa_common_get_nc_providers(aio->common.ucm, node);

  cras_iodev_add_node(&aio->common.base, node);
  check_auto_unplug_input_node(aio, node, node->plugged);
  return input;
}

static void new_input_by_mixer_control(struct mixer_control* cras_input,
                                       void* callback_arg) {
  struct alsa_io* aio = (struct alsa_io*)callback_arg;
  const char* ctl_name = cras_alsa_mixer_get_control_name(cras_input);
  new_input(aio, cras_input, ctl_name);
}

/*
 * Returns the dsp name specified in the ucm config. If there is a dsp name
 * specified for the active node, use that. Otherwise NULL should be returned.
 */
static const char* get_active_dsp_name(struct alsa_io* aio) {
  struct cras_ionode* node = aio->common.base.active_node;

  if (node == NULL) {
    return NULL;
  }

  return node->dsp_name;
}

/*
 * Creates volume curve for the node associated with given jack.
 */
static struct cras_volume_curve* create_volume_curve_for_output(
    const struct cras_card_config* config,
    const struct alsa_output_node* aout) {
  struct cras_volume_curve* curve;
  const char* name;

  // Use node's name as key to get volume curve.
  name = aout->common.base.name;
  curve = cras_card_config_get_volume_curve_for_control(config, name);
  if (curve) {
    return curve;
  }

  if (aout->common.jack == NULL) {
    return NULL;
  }

  // Use jack's UCM device name as key to get volume curve.
  name = cras_alsa_jack_get_ucm_device(aout->common.jack);
  curve = cras_card_config_get_volume_curve_for_control(config, name);
  if (curve) {
    return curve;
  }

  // Use alsa jack's name as key to get volume curve.
  name = cras_alsa_jack_get_name(aout->common.jack);
  curve = cras_card_config_get_volume_curve_for_control(config, name);
  if (curve) {
    return curve;
  }

  return NULL;
}

/* 1. Create volume curve for nodes base on cras config.
 * 2. Finalize volume settings including SW or HW volume.
 * 3. Build software volume scaler.
 */
static void finalize_volume_settings(struct alsa_output_node* output,
                                     struct alsa_io* aio) {
  const struct cras_volume_curve* curve;
  struct cras_ionode* node = &output->common.base;

  output->volume_curve =
      create_volume_curve_for_output(aio->common.config, output);

  node->number_of_volume_steps = NUMBER_OF_VOLUME_STEPS_DEFAULT;

  /* Use software volume for HDMI output and nodes without volume mixer
   * control. */
  if ((node->type == CRAS_NODE_TYPE_HDMI) ||
      (!cras_alsa_mixer_has_main_volume(aio->common.mixer) &&
       !cras_alsa_mixer_has_volume(output->common.mixer))) {
    node->software_volume_needed = 1;
    syslog(LOG_DEBUG, "Use software volume for node: %s", node->name);
  }

  curve = get_curve_for_output_node(aio, output);
  node->softvol_scalers = softvol_build_from_curve(curve);
}

/*
 * Updates max_supported_channels value into cras_iodev_info.
 * Note that supported_rates, supported_channel_counts, and supported_formats of
 * iodev will be updated to the latest values after calling.
 */
static void update_max_supported_channels(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
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
    syslog(LOG_ERR,
           "update_max_supported_channels should not be called "
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

  if (iodev->active_node &&
      iodev->active_node->type == CRAS_NODE_TYPE_INTERNAL_SPEAKER) {
    max_channels = cras_system_get_max_internal_speaker_channels();
    goto update_info;
  }

  if (iodev->active_node &&
      (iodev->active_node->type == CRAS_NODE_TYPE_HEADPHONE ||
       iodev->active_node->type == CRAS_NODE_TYPE_LINEOUT)) {
    max_channels = cras_system_get_max_headphone_channels();
    goto update_info;
  }

  rc = open_dev(iodev);
  if (active_node_predicted) {
    iodev->active_node = NULL;  // Reset the predicted active_node.
  }
  if (rc) {
    goto update_info;
  }

  rc = update_supported_formats(iodev);
  if (rc) {
    goto close_iodev;
  }

  for (i = 0; iodev->supported_channel_counts[i] != 0; i++) {
    if (iodev->supported_channel_counts[i] > max_channels) {
      max_channels = iodev->supported_channel_counts[i];
    }
  }

close_iodev:
  close_dev(iodev);

update_info:
  iodev->info.max_supported_channels = max_channels;
}

/*
 * Callback that is called when an output jack is plugged or unplugged.
 */
static void jack_output_plug_event(const struct cras_alsa_jack* jack,
                                   int plugged,
                                   void* arg) {
  if (arg == NULL) {
    return;
  }

  struct alsa_io* aio = (struct alsa_io*)arg;
  struct alsa_common_node* anode =
      cras_alsa_get_node_from_jack(&aio->common, jack);
  const char* jack_name = cras_alsa_jack_get_name(jack);

  if (anode == NULL) {
    syslog(LOG_ERR, "No matching node found for jack %s", jack_name);
    return;
  }
  if (anode->jack == NULL) {
    syslog(LOG_ERR, "Jack %s isn't associated with the matched node",
           jack_name);
    return;
  }

  syslog(LOG_DEBUG, "%s plugged: %d, %s", jack_name, plugged,
         cras_alsa_mixer_get_control_name(anode->mixer));

  struct cras_ionode* node = &anode->base;
  cras_alsa_jack_update_monitor_name(jack, node->name, sizeof(node->name));
  if (node->type == CRAS_NODE_TYPE_HDMI && plugged) {
    node->stable_id = cras_alsa_jack_get_monitor_stable_id(
        jack, node->name, aio->common.base.info.stable_id);
  }
  // The name got from jack might be an invalid UTF8 string.
  if (!is_utf8_string(node->name)) {
    drop_node_name(node);
  }

  cras_iodev_set_node_plugged(node, plugged);

  check_auto_unplug_output_node(aio, node, plugged);

  /*
   * For HDMI plug event cases, update max supported channels according
   * to the current active node.
   */
  if (node->type == CRAS_NODE_TYPE_HDMI && plugged) {
    update_max_supported_channels(&aio->common.base);
  }
}

/*
 * Callback that is called when an input jack is plugged or unplugged.
 */
static void jack_input_plug_event(const struct cras_alsa_jack* jack,
                                  int plugged,
                                  void* arg) {
  if (arg == NULL) {
    return;
  }
  struct alsa_io* aio = (struct alsa_io*)arg;
  struct alsa_common_node* node =
      cras_alsa_get_node_from_jack(&aio->common, jack);
  const char* jack_name = cras_alsa_jack_get_name(jack);

  if (node == NULL) {
    syslog(LOG_ERR, "No matching node found for jack %s", jack_name);
    return;
  }
  if (node->jack == NULL) {
    syslog(LOG_ERR, "Jack %s isn't associated with the matched node",
           jack_name);
    return;
  }

  syslog(LOG_DEBUG, "%s plugged: %d, %s", jack_name, plugged,
         cras_alsa_mixer_get_control_name(node->mixer));

  cras_iodev_set_node_plugged(&node->base, plugged);

  check_auto_unplug_input_node(aio, &node->base, plugged);
}

/*
 * Sets the name of the given iodev, using the name and index of the card
 * combined with the device index and direction.
 */
static void set_iodev_name(struct cras_iodev* dev,
                           const char* card_name,
                           const char* dev_name,
                           size_t card_index,
                           size_t device_index,
                           enum CRAS_ALSA_CARD_TYPE card_type) {
  snprintf(dev->info.name, sizeof(dev->info.name), "%s: %s:%zu,%zu", card_name,
           dev_name, card_index, device_index);
  dev->info.name[ARRAY_SIZE(dev->info.name) - 1] = '\0';
  syslog(LOG_DEBUG, "Add device name=%s", dev->info.name);

  dev->info.stable_id =
      SuperFastHash(card_name, strlen(card_name), strlen(card_name));
  dev->info.stable_id =
      SuperFastHash(dev_name, strlen(dev_name), dev->info.stable_id);

  switch (card_type) {
    case ALSA_CARD_TYPE_INTERNAL:
    case ALSA_CARD_TYPE_HDMI:
      dev->info.stable_id =
          SuperFastHash((const char*)&device_index, sizeof(device_index),
                        dev->info.stable_id);
      break;
    default:
      break;
  }
  syslog(LOG_DEBUG, "Stable ID=%08x", dev->info.stable_id);
}

static int copy_formats_from_open_dev(struct cras_iodev* dst,
                                      struct cras_iodev* src) {
  if (!src->format) {
    return -ENOENT;
  }

  // supported_rates/channel_counts/formats are zero terminated arrays.
  dst->supported_rates = (size_t*)calloc(2, sizeof(dst->supported_rates[0]));
  if (!dst->supported_rates) {
    return -ENOMEM;
  }
  dst->supported_rates[0] = src->format->frame_rate;
  dst->supported_rates[1] = 0;

  dst->supported_channel_counts =
      (size_t*)calloc(2, sizeof(dst->supported_channel_counts[0]));
  if (!dst->supported_channel_counts) {
    return -ENOMEM;
  }
  dst->supported_channel_counts[0] = src->format->num_channels;
  dst->supported_channel_counts[1] = 0;

  dst->supported_formats =
      (snd_pcm_format_t*)calloc(2, sizeof(dst->supported_formats[0]));
  if (!dst->supported_formats) {
    return -ENOMEM;
  }
  dst->supported_formats[0] = src->format->format;
  // Array terminated by zero (SND_PCM_FORMAT_S8), see get_best_pcm_format.
  dst->supported_formats[1] = (snd_pcm_format_t)0;

  return 0;
}

/*
 * Updates the supported sample rates and channel counts.
 */
static int update_supported_formats(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  int err;
  int fixed_rate;
  size_t fixed_channels;

  free(iodev->supported_rates);
  iodev->supported_rates = NULL;
  free(iodev->supported_channel_counts);
  iodev->supported_channel_counts = NULL;
  free(iodev->supported_formats);
  iodev->supported_formats = NULL;

  // If aio is in a group with an open dev, use the open dev's audio formats.
  struct cras_iodev* open_dev_in_group = NULL;
  if (aio->group) {
    for (size_t i = 0; i < aio->group->num_devs; i++) {
      struct cras_iodev* cdev = &aio->group->devs[i]->common.base;

      if (cdev != iodev && cras_iodev_is_open(cdev)) {
        open_dev_in_group = cdev;
        break;
      }
    }
  }
  if (open_dev_in_group) {
    syslog(LOG_DEBUG, "Use existing audio formats");

    err = copy_formats_from_open_dev(iodev, open_dev_in_group);
    if (err) {
      syslog(LOG_ERR, "Failed to copy formats from open dev %s to %s: %d",
             open_dev_in_group->info.name, iodev->info.name, err);
    }

    return err;
  }

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

static void enable_active_ucm(struct alsa_io* aio, int plugged) {
  const struct alsa_common_node* anode =
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

static int fill_whole_buffer_with_zeros(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  int rc;
  uint8_t* dst = NULL;
  size_t format_bytes;

  // Fill whole buffer with zeros.
  rc = cras_alsa_mmap_get_whole_buffer(aio->common.handle, &dst);

  if (rc < 0) {
    syslog(LOG_WARNING, "Failed to get whole buffer: %s", snd_strerror(rc));
    return rc;
  }
  format_bytes = cras_get_format_bytes(iodev->format);
  memset(dst, 0, iodev->buffer_size * format_bytes);
  cras_iodev_stream_offset_reset_all(iodev);

  return iodev->buffer_size;
}

/*
 * Move appl_ptr to min_buffer_level + min_cb_level frames ahead of hw_ptr
 * when resuming from free run.
 */
static int adjust_appl_ptr_for_leaving_free_run(struct cras_iodev* odev) {
  struct alsa_io* aio = (struct alsa_io*)odev;
  snd_pcm_uframes_t ahead;

  ahead = odev->min_buffer_level + odev->min_cb_level;
  return cras_alsa_resume_appl_ptr(aio->common.handle, ahead, NULL);
}

/*
 * Move appl_ptr to min_buffer_level + min_cb_level * 1.5 frames ahead of
 * hw_ptr when adjusting appl_ptr from underrun.
 */
static int adjust_appl_ptr_for_underrun(struct cras_iodev* odev) {
  struct alsa_io* aio = (struct alsa_io*)odev;
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
static int adjust_appl_ptr_samples_remaining(struct cras_iodev* odev) {
  struct alsa_io* aio = (struct alsa_io*)odev;
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
   * If underrun happened, handle it. Because alsa_output_underrun function
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

static int alsa_output_underrun(struct cras_iodev* odev) {
  int rc, filled_frames;

  /* Fill whole buffer with zeros. This avoids samples left in buffer causing
   * noise when device plays them. */
  filled_frames = fill_whole_buffer_with_zeros(odev);
  if (filled_frames < 0) {
    return filled_frames;
  }

  // Adjust appl_ptr to leave underrun.
  rc = adjust_appl_ptr_for_underrun(odev);
  if (rc < 0) {
    return rc;
  }

  return filled_frames;
}

static int possibly_enter_free_run(struct cras_iodev* odev) {
  struct alsa_io* aio = (struct alsa_io*)odev;
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
    rc = fill_whole_buffer_with_zeros(odev);
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

static int leave_free_run(struct cras_iodev* odev) {
  struct alsa_io* aio = (struct alsa_io*)odev;
  int rc;

  /* Restart rate estimation because free run internval should not
   * be included. */
  cras_iodev_reset_rate_estimator(odev);

  if (aio->common.free_running) {
    rc = adjust_appl_ptr_for_leaving_free_run(odev);
  } else {
    rc = adjust_appl_ptr_samples_remaining(odev);
  }
  if (rc < 0) {
    syslog(LOG_WARNING, "device %s failed to leave free run, rc = %d",
           odev->info.name, rc);
    return rc;
  }
  aio->common.free_running = 0;
  aio->common.filled_zeros_for_draining = 0;

  return 0;
}

/*
 * Free run state is the optimization of no_stream playback on alsa_io.
 * The whole buffer will be filled with zeros. Device can play these zeros
 * indefinitely. When there is new meaningful sample, appl_ptr should be
 * resumed to some distance ahead of hw_ptr.
 */
static int no_stream(struct cras_iodev* odev, int enable) {
  if (enable) {
    return possibly_enter_free_run(odev);
  } else {
    return leave_free_run(odev);
  }
}

static int is_free_running(const struct cras_iodev* odev) {
  struct alsa_io* aio = (struct alsa_io*)odev;

  return aio->common.free_running;
}

static unsigned int get_num_severe_underruns(const struct cras_iodev* iodev) {
  const struct alsa_io* aio = (const struct alsa_io*)iodev;
  return aio->common.num_severe_underruns;
}

static void set_default_hotword_model(struct cras_iodev* iodev) {
  const char* default_models[] = {"en_all", "en_us"};
  cras_node_id_t node_id;
  unsigned i;

  if (!iodev->active_node ||
      iodev->active_node->type != CRAS_NODE_TYPE_HOTWORD) {
    return;
  }

  node_id = cras_make_node_id(iodev->info.idx, iodev->active_node->idx);
  // This is a no-op if the default_model is not supported
  for (i = 0; i < ARRAY_SIZE(default_models); ++i) {
    if (!cras_iodev_list_set_hotword_model(node_id, default_models[i])) {
      return;
    }
  }
}

static int get_valid_frames(struct cras_iodev* odev, struct timespec* tstamp) {
  struct alsa_io* aio = (struct alsa_io*)odev;
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

static int support_noise_cancellation(const struct cras_iodev* iodev,
                                      unsigned node_idx) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  struct cras_ionode* n;

  if (!aio->common.ucm) {
    return 0;
  }

  DL_FOREACH (iodev->nodes, n) {
    if (n->idx == node_idx) {
      struct alsa_common_node* anode = (struct alsa_common_node*)n;
      return ucm_node_noise_cancellation_exists(aio->common.ucm,
                                                anode->ucm_name);
    }
  }
  return 0;
}

static bool set_rtc_proc_enabled(struct cras_iodev* iodev,
                                 enum RTC_PROC_ON_DSP rtc_proc,
                                 bool enabled) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  int rc;

  if (!aio->common.ucm) {
    return false;
  }
  switch (rtc_proc) {
    case RTC_PROC_AEC:
      rc = ucm_enable_node_echo_cancellation(aio->common.ucm, enabled);
      if (rc == 0) {
        aio->dsp_echo_cancellation_enabled = enabled;
      }
      break;
    case RTC_PROC_NS:
      rc = ucm_enable_node_noise_suppression(aio->common.ucm, enabled);
      if (rc == 0) {
        aio->dsp_noise_suppression_enabled = enabled;
      }
      break;
    case RTC_PROC_AGC:
      rc = ucm_enable_node_gain_control(aio->common.ucm, enabled);
      if (rc == 0) {
        aio->dsp_gain_control_enabled = enabled;
      }
      break;
    default:
      return false;
  }
  return true;
}

static bool get_rtc_proc_enabled(struct cras_iodev* iodev,
                                 enum RTC_PROC_ON_DSP rtc_proc) {
  struct alsa_io* aio = (struct alsa_io*)iodev;

  switch (rtc_proc) {
    case RTC_PROC_AEC:
      return aio->dsp_echo_cancellation_enabled;
    case RTC_PROC_NS:
      return aio->dsp_noise_suppression_enabled;
    case RTC_PROC_AGC:
      return aio->dsp_gain_control_enabled;
    default:
      return false;
  }
  return false;
}

static struct cras_iodev* const* get_dev_group(const struct cras_iodev* iodev,
                                               size_t* out_group_size) {
  const struct alsa_io* aio = (const struct alsa_io*)iodev;

  if (out_group_size) {
    *out_group_size = aio->group ? aio->group->num_devs : 0;
  }
  return aio->group ? (struct cras_iodev* const*)aio->group->devs : NULL;
}

static uintptr_t get_dev_group_id(const struct cras_iodev* iodev) {
  const struct alsa_io* aio = (const struct alsa_io*)iodev;

  return (uintptr_t)aio->group;
}

static bool iodev_group_has_use_case(struct alsa_io_group* group,
                                     enum CRAS_USE_CASE use_case) {
  if (!group) {
    return false;
  }

  for (size_t i = 0; i < group->num_devs; i++) {
    if (group->devs[i] && group->devs[i]->use_case == use_case) {
      return true;
    }
  }
  return false;
}

static int should_attach_stream(const struct cras_iodev* iodev,
                                const struct cras_rstream* stream) {
  const struct alsa_io* aio = (const struct alsa_io*)iodev;
  enum CRAS_USE_CASE target_use_case = CRAS_USE_CASE_HIFI;
  int attach;

  // TODO(b/266790044): Add stream-iodev matching logic here based on stream
  // parameters. All streams are assigned to HiFi devices currently.
  attach = target_use_case == aio->use_case;

  syslog(LOG_DEBUG,
         "should_attach_stream: %d {stream=0x%x, target=%s} "
         "{dev=%s, group=%p, use=%s}",
         attach, stream->stream_id, cras_use_case_str(target_use_case),
         aio->common.pcm_name, aio->group, cras_use_case_str(aio->use_case));
  return attach;
}

static int add_to_iodev_group(struct alsa_io_group* group,
                              struct alsa_io* aio) {
  if (!group) {
    return -EINVAL;
  }

  // Skip if the iodev is already in the group.
  if (aio->group == group) {
    return 0;
  }

  if (iodev_group_has_use_case(group, aio->use_case)) {
    syslog(LOG_ERR, "Iodev group already has %s when adding %s",
           cras_use_case_str(aio->use_case), aio->common.base.info.name);
    return -EEXIST;
  }

  if (group->num_devs >= ARRAY_SIZE(group->devs)) {
    return -E2BIG;
  }

  group->devs[group->num_devs++] = aio;
  aio->group = group;
  return 0;
}

static void remove_from_iodev_group(struct alsa_io* aio) {
  size_t i;

  if (!aio->group) {
    return;
  }

  // The order of devs in the group array is changed after removal.
  // The last dev in the array is moved to fill the hole.
  for (i = 0; i < aio->group->num_devs; i++) {
    if (aio->group->devs[i] == aio) {
      aio->group->devs[i] = aio->group->devs[--aio->group->num_devs];
      break;
    }
  }

  // alsa_io_group is ref-counted and owned by its member devices.
  if (!aio->group->num_devs) {
    free(aio->group);
  }
  aio->group = NULL;
}

static struct alsa_io_group* create_iodev_group(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  struct alsa_io_group* group;

  if (aio->group) {
    return aio->group;
  }

  group = (struct alsa_io_group*)calloc(1, sizeof(*group));
  if (group == NULL) {
    syslog(LOG_ERR, "Out of memory when creating alsa io group.");
    return NULL;
  }

  // Add self.
  add_to_iodev_group(group, aio);
  return group;
}

enum CRAS_USE_CASE get_use_case(const struct cras_iodev* iodev) {
  const struct alsa_io* aio = (const struct alsa_io*)iodev;

  return aio->use_case;
}

static void cras_iodev_update_speaker_rotation(struct cras_iodev* iodev) {
  // Only 4 speaker devices need the speaker rotation operations.
  if (!iodev->active_node ||
      iodev->active_node->type != CRAS_NODE_TYPE_INTERNAL_SPEAKER ||
      !iodev->format || iodev->format->num_channels != 4) {
    return;
  }
  cras_iodev_update_dsp(iodev);
}

/*
 * Exported Interface.
 */

struct cras_iodev* alsa_iodev_create(
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
  struct alsa_io* aio;
  struct cras_iodev* iodev;

  if (!card_info) {
    return NULL;
  }

  if (direction != CRAS_STREAM_INPUT && direction != CRAS_STREAM_OUTPUT) {
    return NULL;
  }

  aio = (struct alsa_io*)calloc(1, sizeof(*aio));
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
    aio->common.base.set_volume = set_alsa_volume;
    aio->common.base.set_mute = set_alsa_mute;
    aio->common.base.output_underrun = alsa_output_underrun;
  }
  iodev->open_dev = open_dev;
  iodev->configure_dev = configure_dev;
  iodev->close_dev = close_dev;
  iodev->update_supported_formats = update_supported_formats;
  iodev->frames_queued = frames_queued;
  iodev->delay_frames = delay_frames;
  iodev->get_buffer = get_buffer;
  iodev->put_buffer = put_buffer;
  iodev->flush_buffer = flush_buffer;
  iodev->start = start;
  iodev->update_active_node = update_active_node;
  iodev->update_channel_layout = update_channel_layout;
  iodev->set_hotword_model = set_hotword_model;
  iodev->get_hotword_models = get_hotword_models;
  iodev->no_stream = no_stream;
  iodev->is_free_running = is_free_running;
  iodev->get_num_severe_underruns = get_num_severe_underruns;
  iodev->get_valid_frames = get_valid_frames;
  iodev->set_swap_mode_for_node = cras_iodev_dsp_set_swap_mode_for_node;
  iodev->display_rotation_changed = cras_iodev_update_speaker_rotation;
  iodev->support_noise_cancellation = support_noise_cancellation;
  iodev->set_rtc_proc_enabled = set_rtc_proc_enabled;
  iodev->get_rtc_proc_enabled = get_rtc_proc_enabled;
  iodev->get_dev_group = get_dev_group;
  iodev->get_dev_group_id = get_dev_group_id;
  iodev->should_attach_stream = should_attach_stream;
  iodev->get_use_case = get_use_case;
  iodev->get_htimestamp = cras_alsa_common_get_htimestamp;

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
      aio->common.base.set_swap_mode_for_node = set_alsa_node_swapped;
    }

    rc = ucm_get_min_buffer_level(ucm, &level);
    if (!rc && direction == CRAS_STREAM_OUTPUT) {
      iodev->min_buffer_level = level;
    }
  }

  set_iodev_name(iodev, card_name, dev_name, card_info->card_index,
                 device_index, card_info->card_type);

  aio->use_case = use_case;
  if (group_ref) {
    struct alsa_io_group* group;
    int rc;

    group = create_iodev_group(group_ref);
    rc = add_to_iodev_group(group, aio);
    if (rc) {
      syslog(LOG_ERR, "Failed to add to iodev group: %d", rc);
      /* Don't create the iodev if it can't be added to the group. An orphaned
       * iodev can't be enabled by iodev_list because it has no ionode, so no
       * stream can be added to it.
       *
       * group_ref may be in a group with only itself if all other iodevs of the
       * group failed to be added. This is still OK. */
      goto cleanup_iodev;
    }
  }

  aio->common.jack_list = cras_alsa_jack_list_create(
      card_info->card_index, card_name, device_index, is_first, mixer, ucm,
      hctl, direction,
      direction == CRAS_STREAM_OUTPUT ? jack_output_plug_event
                                      : jack_input_plug_event,
      aio);
  if (!aio->common.jack_list) {
    goto cleanup_iodev;
  }

  // HDMI outputs don't have volume adjustment, do it in software.
  if (direction == CRAS_STREAM_OUTPUT && strstr(dev_name, HDMI)) {
    iodev->software_volume_needed = 1;
  }

  /* Add this now so that cleanup of the iodev (in case of error or card
   * card removal will function as expected. */
  cras_iodev_list_add(&aio->common.base);
  return &aio->common.base;

cleanup_iodev:
  remove_from_iodev_group(aio);
  free_alsa_iodev_resources(aio);
  free(aio);
  return NULL;
}

// When a jack is found, try to associate it with a node already created
// for mixer control. If there isn't a node can be associated, go for
// creating a new node for the jack.
static void add_input_node_or_associate_jack(const struct cras_alsa_jack* jack,
                                             void* arg) {
  struct alsa_io* aio;
  struct alsa_common_node* node;
  struct mixer_control* cras_input;
  const char* jack_name;

  CRAS_CHECK(arg);

  aio = (struct alsa_io*)arg;
  node = cras_alsa_get_node_from_jack(&aio->common, jack);
  jack_name = cras_alsa_jack_get_name(jack);

  // If there isn't a node for this jack, create one.
  if (node == NULL) {
    cras_input = cras_alsa_jack_get_mixer(jack);
    node = (struct alsa_common_node*)new_input(aio, cras_input, jack_name);
    if (node == NULL) {
      return;
    }
  }

  // If we already have the node, associate with the jack.
  if (!node->jack) {
    node->jack = jack;
  }
}

static void add_output_node_or_associate_jack(const struct cras_alsa_jack* jack,
                                              void* arg) {
  struct alsa_io* aio;
  struct alsa_common_node* node;
  const char* jack_name;

  CRAS_CHECK(arg);

  aio = (struct alsa_io*)arg;
  node = cras_alsa_get_node_from_jack(&aio->common, jack);
  jack_name = cras_alsa_jack_get_name(jack);
  if (!jack_name || !strcmp(jack_name, "Speaker Phantom Jack")) {
    jack_name = INTERNAL_SPEAKER;
  }

  // If there isn't a node for this jack, create one.
  if (node == NULL) {
    node = (struct alsa_common_node*)new_output(aio, NULL, jack_name);
    if (node == NULL) {
      return;
    }

    cras_alsa_jack_update_node_type(jack, &node->base.type);
  }

  if (!node->jack) {
    // If we already have the node, associate with the jack.
    node->jack = jack;
  }
}

int alsa_iodev_legacy_complete_init(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  const char* dev_name;
  const char* dev_id;
  enum CRAS_STREAM_DIRECTION direction;
  int err;
  int is_first;
  struct cras_alsa_mixer* mixer;

  if (!aio) {
    return -EINVAL;
  }
  direction = iodev->direction;
  dev_name = aio->common.dev_name;
  dev_id = aio->common.dev_id;
  is_first = aio->common.is_first;
  mixer = aio->common.mixer;

  /* Create output nodes for mixer controls, such as Headphone
   * and Speaker, only for the first device. */
  if (direction == CRAS_STREAM_OUTPUT && is_first) {
    cras_alsa_mixer_list_outputs(mixer, new_output_by_mixer_control, aio);
  } else if (direction == CRAS_STREAM_INPUT && is_first) {
    cras_alsa_mixer_list_inputs(mixer, new_input_by_mixer_control, aio);
  }

  err = cras_alsa_jack_list_find_jacks_by_name_matching(
      aio->common.jack_list,
      iodev->direction == CRAS_STREAM_OUTPUT ? add_output_node_or_associate_jack
                                             : add_input_node_or_associate_jack,
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
      !no_create_default_output_node(aio)) {
    if (first_internal_device(aio) && !has_node(aio, INTERNAL_SPEAKER) &&
        !has_node(aio, HDMI)) {
      if (strstr(aio->common.base.info.name, HDMI)) {
        new_output(aio, NULL, HDMI);
      } else {
        new_output(aio, NULL, INTERNAL_SPEAKER);
      }
    } else if (!aio->common.base.nodes) {
      new_output(aio, NULL, DEFAULT);
    }
  } else if ((direction == CRAS_STREAM_INPUT) &&
             !no_create_default_input_node(aio)) {
    if (first_internal_device(aio) && !has_node(aio, INTERNAL_MICROPHONE)) {
      new_input(aio, NULL, INTERNAL_MICROPHONE);
    } else if (strstr(dev_name, KEYBOARD_MIC)) {
      new_input(aio, NULL, KEYBOARD_MIC);
    } else if (dev_id && strstr(dev_id, HOTWORD_DEV)) {
      new_input(aio, NULL, HOTWORD_DEV);
    } else if (!aio->common.base.nodes) {
      new_input(aio, NULL, DEFAULT);
    }
  }

  // Finalize volume settings for output nodes.
  if (direction == CRAS_STREAM_OUTPUT) {
    struct cras_ionode* node;
    DL_FOREACH (iodev->nodes, node) {
      finalize_volume_settings((struct alsa_output_node*)node, aio);
    }
  }

  // Set the active node as the best node we have now.
  alsa_iodev_set_active_node(&aio->common.base,
                             first_plugged_node(&aio->common.base), 0);

  set_default_hotword_model(iodev);

  // Record max supported channels into cras_iodev_info.
  update_max_supported_channels(iodev);

  return 0;
}

int alsa_iodev_ucm_add_nodes_and_jacks(struct cras_iodev* iodev,
                                       struct ucm_section* section) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
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
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    struct alsa_output_node* output_node =
        new_output(aio, control, section->name);
    if (!output_node) {
      return -ENOMEM;
    }
    anode = &output_node->common;
  } else if (iodev->direction == CRAS_STREAM_INPUT) {
    struct alsa_input_node* input_node = new_input(aio, control, section->name);
    if (!input_node) {
      return -ENOMEM;
    }
    anode = &input_node->common;
  }
  if (!anode) {
    return -EINVAL;
  }
  anode->pcm_name = strdup(section->pcm_name);

  // Find any jack controls for this device.
  return cras_alsa_jack_list_add_jack_for_section(
      aio->common.jack_list, section, (struct cras_alsa_jack**)&anode->jack);
}

void alsa_iodev_ucm_complete_init(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  struct cras_ionode* node;

  if (!iodev) {
    return;
  }

  // Get an initial read of the jacks for this device.
  cras_alsa_jack_list_report(aio->common.jack_list);

  // Finalize volume settings for output nodes.
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    DL_FOREACH (iodev->nodes, node) {
      finalize_volume_settings((struct alsa_output_node*)node, aio);
    }
  }

  // Create the mapping information for DSP offload if the device has any node
  // supporting offload.
  aio->common.base.dsp_offload_map = NULL;
  if (aio->common.base.nodes) {
    DL_FOREACH (aio->common.base.nodes, node) {
      struct dsp_offload_map* offload_map;
      int rc = cras_dsp_offload_create_map(&offload_map, node);
      if (rc) {
        syslog(LOG_INFO,
               "DSP offload is not supported for specified node: %s, rc=%d",
               node->name, rc);
        break;
      }
      if (offload_map) {
        aio->common.base.dsp_offload_map = offload_map;
        break;
      }
      // (!rc && !offload_map) stands for the node is not supposed to have
      // offload support, i.e. it's not specified in the mapping info. Continue
      // on checking other nodes.
    }
  }

  /* In an iodev group the ionodes are shared by all member iodevs conceptually.
   * However the ionodes are attached to and owned by one iodev
   * (CRAS_USE_CASE_HIFI) in the group, so the active node only needs to be set
   * once per group during init. */
  if (aio->common.base.nodes) {
    if (aio->group) {
      aio->group->nodes_owner = aio;
    }
    // Set the active node as the best node we have now.
    alsa_iodev_set_active_node(&aio->common.base,
                               first_plugged_node(&aio->common.base), 0);
  }

  set_default_hotword_model(iodev);

  node = iodev->active_node;

  /*
   * Record max supported channels into cras_iodev_info.
   * Not check HDMI nodes here if they are unplugged because:
   * 1. The supported channels are different on different HDMI devices.
   * 2. It may cause some problems when open HDMI devices without plugging any
   *    device. (b/170923644)
   */
  if (node && (node->plugged || node->type != CRAS_NODE_TYPE_HDMI)) {
    update_max_supported_channels(iodev);
  }

  /*
   * Regard BT HFP offload is supported and store the statement while the
   * SCO_LINE_IN node is present. For HFP offload the input and output node
   * are always supported in pairs so checkcking either one is sufficient.
   */
  if (node && str_equals(node->name, SCO_LINE_IN)) {
    cras_system_set_bt_hfp_offload_supported(true);
  }
}

void alsa_iodev_destroy(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  int rc = cras_iodev_list_rm(iodev);

  if (rc == -EBUSY) {
    syslog(LOG_WARNING, "Failed to remove iodev %s", iodev->info.name);
    return;
  }

  // Free resources when device successfully removed.
  cras_alsa_jack_list_destroy(aio->common.jack_list);
  remove_from_iodev_group(aio);
  free_alsa_iodev_resources(aio);
  cras_volume_curve_destroy(aio->common.default_volume_curve);
  free(iodev);
}

unsigned alsa_iodev_index(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  return aio->common.device_index;
}

int alsa_iodev_has_hctl_jacks(struct cras_iodev* iodev) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  return cras_alsa_jack_list_has_hctl_jacks(aio->common.jack_list);
}

static void alsa_iodev_unmute_node(struct alsa_io* aio,
                                   struct cras_ionode* ionode) {
  struct alsa_output_node* active = (struct alsa_output_node*)ionode;
  struct mixer_control* mixer = active->common.mixer;
  struct alsa_output_node* output;
  struct cras_ionode* node;

  /* If this node is associated with mixer output, unmute the
   * active mixer output and mute all others, otherwise just set
   * the node as active and set the volume curve. */
  if (mixer) {
    // Unmute the active mixer output, mute all others.
    DL_FOREACH (aio->common.base.nodes, node) {
      output = (struct alsa_output_node*)node;
      if (output->common.mixer) {
        cras_alsa_mixer_set_output_active_state(output->common.mixer,
                                                node == ionode);
      }
    }
  }
}

static int alsa_iodev_set_active_node(struct cras_iodev* iodev,
                                      struct cras_ionode* ionode,
                                      unsigned dev_enabled) {
  struct alsa_io* aio = (struct alsa_io*)iodev;
  int rc = 0;

  if (iodev->active_node == ionode) {
    goto skip;
  }

  // Disable jack ucm before switching node.
  enable_active_ucm(aio, 0);
  if (dev_enabled && (iodev->direction == CRAS_STREAM_OUTPUT)) {
    alsa_iodev_unmute_node(aio, ionode);
  }

  cras_alsa_common_set_active_node(iodev, ionode);
  aio->common.base.dsp_name = get_active_dsp_name(aio);
  cras_iodev_update_dsp(iodev);
skip:
  enable_active_ucm(aio, dev_enabled);
  if (aio->common.ucm && ionode->type == CRAS_NODE_TYPE_HOTWORD) {
    if (dev_enabled) {
      rc = ucm_enable_hotword_model(aio->common.ucm);
      if (rc < 0) {
        return rc;
      }
    } else {
      ucm_disable_all_hotword_models(aio->common.ucm);
    }
  }
  // Setting the volume will also unmute if the system isn't muted.
  init_device_settings(aio);
  return 0;
}

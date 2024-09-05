/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/src/server/cras_alsa_common_io.h"

#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>
#include <time.h>

#include "cras/common/rust_common.h"
#include "cras/src/common/cras_alsa_card_info.h"
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/cras_alsa_helpers.h"
#include "cras/src/server/cras_alsa_jack.h"
#include "cras/src/server/cras_alsa_ucm.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_system_state.h"
#include "cras_iodev_info.h"
#include "cras_types.h"
#include "third_party/utlist/utlist.h"

struct cras_ionode* first_plugged_node(struct cras_iodev* iodev) {
  struct cras_ionode* n;

  /* When this is called at iodev creation, none of the nodes
   * are selected. Just pick the first plugged one and let Chrome
   * choose it later. */
  DL_FOREACH (iodev->nodes, n) {
    if (n->plugged) {
      return n;
    }
  }
  return iodev->nodes;
}

int cras_alsa_common_configure_noise_cancellation(
    struct cras_iodev* iodev,
    struct cras_use_case_mgr* ucm) {
  iodev->restart_tag_effect_state = cras_iodev_list_resolve_nc_provider(iodev);

  if (iodev->active_node &&
      iodev->active_node->nc_providers & CRAS_NC_PROVIDER_DSP) {
    bool enable_dsp_noise_cancellation =
        iodev->restart_tag_effect_state == CRAS_NC_PROVIDER_DSP;

    int rc = ucm_enable_node_noise_cancellation(ucm, iodev->active_node->name,
                                                enable_dsp_noise_cancellation);
    if (rc < 0) {
      return rc;
    }

    enum CRAS_NOISE_CANCELLATION_STATUS nc_status;
    if (!iodev->restart_tag_effect_state) {
      nc_status = CRAS_NOISE_CANCELLATION_DISABLED;
    } else if (enable_dsp_noise_cancellation) {
      nc_status = CRAS_NOISE_CANCELLATION_ENABLED;
    } else {
      nc_status = CRAS_NOISE_CANCELLATION_BLOCKED;
    }

    cras_server_metrics_device_noise_cancellation_status(iodev, nc_status);
  }

  return 0;
}

CRAS_NC_PROVIDER cras_alsa_common_get_nc_providers(
    struct cras_use_case_mgr* ucm,
    const struct cras_ionode* node) {
  CRAS_NC_PROVIDER provider = 0;
  if ((node->type == CRAS_NODE_TYPE_ALSA_LOOPBACK ||  // Alsa loopback.
       (node->type == CRAS_NODE_TYPE_MIC &&           // Internal mic.
        (node->position == NODE_POSITION_INTERNAL ||
         node->position == NODE_POSITION_FRONT))) &&
      cras_system_get_style_transfer_supported()) {  // Supportness.
    provider |= CRAS_NC_PROVIDER_AST;
  }
  if (ucm && cras_system_get_dsp_noise_cancellation_supported() &&
      ucm_node_noise_cancellation_exists(ucm, node->name)) {
    provider |= CRAS_NC_PROVIDER_DSP;
  }
  provider |= CRAS_NC_PROVIDER_AP;
  return provider;
}

int cras_alsa_common_set_hwparams(struct cras_iodev* iodev, int period_wakeup) {
  struct alsa_common_io* aio = (struct alsa_common_io*)iodev;
  int rc;

  // Only need to set hardware params once.
  if (aio->hwparams_set) {
    return 0;
  }

  /* Sets frame rate and channel count to alsa device before
   * we test channel mapping. */
  rc = cras_alsa_set_hwparams(aio->handle, iodev->format, &iodev->buffer_size,
                              period_wakeup, aio->dma_period_set_microsecs);
  if (rc < 0) {
    syslog(
        LOG_ERR,
        "card type: %s, pcm_name: %s, Fail to set hwparams format_rate: %zu, "
        "num_channels: %zu, buffer_size: %ld, period_wakeup: %d, "
        "dma_period_set_microsecs: %d",
        cras_card_type_to_string(aio->card_type), aio->pcm_name,
        iodev->format->frame_rate, iodev->format->num_channels,
        iodev->buffer_size, period_wakeup, aio->dma_period_set_microsecs);

    /* Some devices report incorrect channel capabilities and fail to
       set_hwparams. Retry set_hwparams by using stereo channels to increase the
       success rate of using these devices.
    */
    if (iodev->format->num_channels != 2 &&
        cras_iodev_is_channel_count_supported(iodev, 2)) {
      syslog(LOG_INFO,
             "card type: %s, pcm_name: %s, retry set hwparams with stereo",
             cras_card_type_to_string(aio->card_type), aio->pcm_name);
      iodev->format->num_channels = 2;
      rc = cras_alsa_set_hwparams(aio->handle, iodev->format,
                                  &iodev->buffer_size, period_wakeup,
                                  aio->dma_period_set_microsecs);
      if (rc < 0) {
        syslog(LOG_ERR,
               "failed to retry set hwparams with stereo card type: %s, "
               "pcm_name: %s",
               cras_card_type_to_string(aio->card_type), aio->pcm_name);
        return rc;
      }
      aio->hwparams_set = 1;
    }
    return rc;
  }

  aio->hwparams_set = 1;
  return 0;
}

int cras_alsa_common_frames_queued(const struct cras_iodev* iodev,
                                   struct timespec* tstamp) {
  struct alsa_common_io* aio = (struct alsa_common_io*)iodev;
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
  aio->hardware_timestamp = *tstamp;
  rc = clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
  if (rc < 0) {
    return rc;
  }
  if (iodev->direction == CRAS_STREAM_INPUT) {
    return (int)frames;
  }

  // For output, return number of frames that are used.
  return iodev->buffer_size - frames;
}
int cras_alsa_common_set_active_node(struct cras_iodev* iodev,
                                     struct cras_ionode* ionode) {
  struct alsa_common_io* aio = (struct alsa_common_io*)iodev;
  cras_iodev_set_active_node(iodev, ionode);
  syslog(LOG_INFO,
         "card type: %s, Set active node. name: %s, id: %d, direction: %s, "
         "type: %s, "
         "enable software volume: %d, intrinsic_sensitivity: %ld, volume: %d, "
         "number_of_volume_steps: %d",
         cras_card_type_to_string(aio->card_type), ionode->name, ionode->idx,
         iodev->direction == CRAS_STREAM_OUTPUT ? "output" : "input",
         cras_node_type_to_str(ionode->type, ionode->position),
         ionode->software_volume_needed, ionode->intrinsic_sensitivity,
         ionode->volume, ionode->number_of_volume_steps);
  return 0;
}

int cras_alsa_common_delay_frames(const struct cras_iodev* iodev) {
  struct alsa_common_io* aio = (struct alsa_common_io*)iodev;
  snd_pcm_sframes_t delay;
  int rc;

  rc = cras_alsa_get_delay_frames(aio->handle, iodev->buffer_size, &delay);
  if (rc < 0) {
    return rc;
  }

  return (int)delay;
}

int cras_alsa_common_close_dev(const struct cras_iodev* iodev) {
  struct alsa_common_io* aio = (struct alsa_common_io*)iodev;
  int ret;

  // Removes audio thread callback from main thread.
  if (aio->poll_fd >= 0) {
    ret = audio_thread_rm_callback_sync(cras_iodev_list_get_audio_thread(),
                                        aio->poll_fd);
    if (ret < 0) {
      syslog(LOG_WARNING, "card type: %s ALSA: failed to rm callback sync: %d",
             cras_card_type_to_string(aio->card_type), ret);
    }
  }
  if (!aio->handle) {
    return 0;
  }
  ret = cras_alsa_pcm_close(aio->handle);
  if (ret < 0) {
    syslog(LOG_WARNING, "card type: %s ALSA: failed to close pcm: %d",
           cras_card_type_to_string(aio->card_type), ret);
  }
  aio->handle = NULL;
  aio->free_running = 0;
  aio->filled_zeros_for_draining = 0;
  aio->hwparams_set = 0;
  cras_iodev_free_format(&aio->base);
  cras_iodev_free_audio_area(&aio->base);
  free(aio->sample_buf);
  aio->sample_buf = NULL;
  return 0;
}

int cras_alsa_common_open_dev(struct cras_iodev* iodev, const char* pcm_name) {
  struct alsa_common_io* aio = (struct alsa_common_io*)iodev;
  snd_pcm_t* handle;
  int rc;

  /* aio->pcm_name is synthesized from the card name and the device index from
   * PlaybackPCM or CapturePCM. */
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

int cras_alsa_common_get_htimestamp(const struct cras_iodev* iodev,
                                    struct timespec* ts) {
  struct alsa_common_io* aio = (struct alsa_common_io*)iodev;
  *ts = aio->hardware_timestamp;
  return 0;
}

int cras_alsa_get_fixed_rate(struct alsa_common_io* aio) {
  struct alsa_common_node* anode =
      (struct alsa_common_node*)aio->base.active_node;

  if (!anode) {
    return -ENOENT;
  }

  return ucm_get_sample_rate_for_dev(aio->ucm, anode->ucm_name,
                                     aio->base.direction);
}

size_t cras_alsa_get_fixed_channels(struct alsa_common_io* aio) {
  struct alsa_common_node* anode =
      (struct alsa_common_node*)aio->base.active_node;
  int rc;
  size_t channels;

  if (!anode) {
    return -ENOENT;
  }
  rc = ucm_get_channels_for_dev(aio->ucm, anode->ucm_name, aio->base.direction,
                                &channels);
  return (rc) ? 0 : channels;
}

struct alsa_common_node* cras_alsa_get_node_from_jack(
    struct alsa_common_io* aio,
    const struct cras_alsa_jack* jack) {
  struct mixer_control* mixer;
  struct cras_ionode* node = NULL;
  struct alsa_common_node* anode = NULL;

  // Search by jack first.
  DL_SEARCH_SCALAR_WITH_CAST(aio->base.nodes, node, anode, jack, jack);
  if (anode) {
    return anode;
  }

  // Search by mixer control next.
  mixer = cras_alsa_jack_get_mixer(jack);
  if (mixer == NULL) {
    return NULL;
  }

  DL_SEARCH_SCALAR_WITH_CAST(aio->base.nodes, node, anode, mixer, mixer);
  return anode;
}

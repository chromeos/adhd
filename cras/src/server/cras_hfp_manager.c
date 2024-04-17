/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // for ppoll
#endif

#include "cras/src/server/cras_hfp_manager.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "cras/server/platform/features/features.h"
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_bt_policy.h"
#include "cras/src/server/cras_fl_media.h"
#include "cras/src/server/cras_fl_media_adapter.h"
#include "cras/src/server/cras_fl_pcm_iodev.h"
#include "cras/src/server/cras_hfp_alsa_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_system_state.h"
#include "cras_config.h"
#include "cras_types.h"
#include "third_party/superfasthash/sfh.h"

#define CRAS_HFP_SOCKET_FILE ".hfp"
#define FLOSS_HFP_DATA_PATH "/run/bluetooth/audio/.sco_data"

/*
 * Object holding information and resources of a connected HFP headset.
 */
struct cras_hfp {
  // Object representing the media interface of BT adapter.
  struct fl_media* fm;
  // The input iodev for HFP.
  struct cras_iodev* idev;
  // The output iodev for HFP.
  struct cras_iodev* odev;
  // The address of connected HFP device.
  char* addr;
  // The name of connected hfp device.
  char* name;
  // The file descriptor for SCO socket.
  int fd;
  // If an input device started. This is used to determine if
  // a sco start or stop is required.
  int idev_started;
  // If an output device started. This is used to determine if
  // a sco start or stop is required.
  int odev_started;
  // Every successful |StartScoCall| should expect an audio disconnection
  // event callback. If the event comes before |StopScoCall|, we will
  // issue a reconnection request.
  bool is_sco_stopped;
  int hfp_caps;
  enum HFP_CODEC_FORMAT active_codec_format;
  bool sco_pcm_used;
};

void fill_floss_hfp_skt_addr(struct sockaddr_un* addr) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  snprintf(addr->sun_path, CRAS_MAX_SOCKET_PATH_SIZE, FLOSS_HFP_DATA_PATH);
}

void set_dev_started(struct cras_hfp* hfp,
                     enum CRAS_STREAM_DIRECTION dir,
                     int started) {
  if (dir == CRAS_STREAM_INPUT) {
    hfp->idev_started = started;
  } else if (dir == CRAS_STREAM_OUTPUT) {
    hfp->odev_started = started;
  }
}

static bool is_sco_pcm_used() {
  /* If board config "bluetooth:hfp_offload_finch_applied" is specified,
   * check the feature state from Chrome Feature Service to determine
   * whether to use HFP offload path; otherwise, always choose HFP offload
   * path.
   */
  if (cras_system_get_bt_hfp_offload_finch_applied()) {
    return cras_feature_enabled(CrOSLateBootAudioHFPOffload);
  }

  return true;
}

static bool is_sco_pcm_supported() {
  return (cras_iodev_list_get_sco_pcm_iodev(CRAS_STREAM_INPUT) ||
          cras_iodev_list_get_sco_pcm_iodev(CRAS_STREAM_OUTPUT));
}

static bool cras_floss_hfp_swb_allowed() {
  if (cras_system_get_force_hfp_swb_enabled()) {
    return true;
  }
  return cras_feature_enabled(CrOSLateBootAudioHFPSwb);
}

/* Creates cras_hfp object representing a connected hfp device. */
struct cras_hfp* cras_floss_hfp_create(struct fl_media* fm,
                                       const char* addr,
                                       const char* name,
                                       int hfp_caps) {
  struct cras_hfp* hfp;
  hfp = (struct cras_hfp*)calloc(1, sizeof(*hfp));

  if (!hfp) {
    return NULL;
  }

  hfp->fm = fm;
  hfp->addr = strdup(addr);
  hfp->name = strdup(name);
  hfp->fd = -1;
  hfp->hfp_caps = hfp_caps;
  hfp->sco_pcm_used = is_sco_pcm_supported() && is_sco_pcm_used();

  if (!cras_system_get_bt_wbs_enabled()) {
    hfp->hfp_caps &= ~HFP_CODEC_FORMAT_MSBC_TRANSPARENT;
    hfp->hfp_caps &= ~HFP_CODEC_FORMAT_MSBC;
  }

  // Until SWB is fully launched, it will be guarded by a flag,
  // which can be enabled by users, experiments, or tests.
  if (!cras_floss_hfp_swb_allowed()) {
    // If SWB is available but we have disabled it, then the peer will almost
    // certainly fallback to WBS. In such event, MSBC transparent should be
    // tried.
    if (hfp->hfp_caps & HFP_CODEC_FORMAT_LC3_TRANSPARENT) {
      hfp->hfp_caps |= HFP_CODEC_FORMAT_MSBC_TRANSPARENT;
    }
    hfp->hfp_caps &= ~HFP_CODEC_FORMAT_LC3_TRANSPARENT;
  }

  // Currently, SWB is only supported via SW encoding. If CRAS would
  // have been using HW encoding but LC3 is available, we will back off to
  // the SW encoding backend.
  if (cras_floss_hfp_is_codec_format_supported(
          hfp, HFP_CODEC_FORMAT_LC3_TRANSPARENT) &&
      hfp->sco_pcm_used) {
    hfp->sco_pcm_used = false;
    syslog(LOG_INFO, "Bypassed offloading to allow LC3");
    // TODO: add UMA for this case
  }

  if (hfp->sco_pcm_used) {
    struct cras_iodev *in_aio, *out_aio;

    in_aio = cras_iodev_list_get_sco_pcm_iodev(CRAS_STREAM_INPUT);
    out_aio = cras_iodev_list_get_sco_pcm_iodev(CRAS_STREAM_OUTPUT);

    hfp->idev = hfp_alsa_iodev_create(in_aio, NULL, NULL, NULL, hfp);
    hfp->odev = hfp_alsa_iodev_create(out_aio, NULL, NULL, NULL, hfp);
  } else {
    hfp->idev = hfp_pcm_iodev_create(hfp, CRAS_STREAM_INPUT);
    hfp->odev = hfp_pcm_iodev_create(hfp, CRAS_STREAM_OUTPUT);
  }

  BTLOG(btlog, BT_AUDIO_GATEWAY_START,
        (is_sco_pcm_supported() << 1) | hfp->sco_pcm_used, hfp->hfp_caps);

  if (!hfp->idev || !hfp->odev) {
    syslog(LOG_WARNING, "Failed to create hfp pcm_iodev for %s", name);
    cras_floss_hfp_destroy(hfp);
    return NULL;
  }

  return hfp;
}

int cras_floss_hfp_start(struct cras_hfp* hfp,
                         thread_callback cb,
                         enum CRAS_STREAM_DIRECTION dir) {
  int skt_fd;
  int rc;
  struct sockaddr_un addr;
  struct timespec timeout = {10, 0};
  struct pollfd poll_fd;

  if ((dir == CRAS_STREAM_INPUT && hfp->idev_started) ||
      (dir == CRAS_STREAM_OUTPUT && hfp->odev_started)) {
    return -EINVAL;
  }

  /* Check if the sco and socket connection has started by another
   * direction's iodev. We can skip the data channel setup if so. */
  if (cras_floss_hfp_is_sco_running(hfp)) {
    goto start_dev;
  }

  int disabled_codecs = FL_HFP_CODEC_BIT_ID_NONE;
  if (!cras_system_get_bt_wbs_enabled()) {
    disabled_codecs |= FL_HFP_CODEC_BIT_ID_MSBC | FL_HFP_CODEC_BIT_ID_LC3;
  }
  if (hfp->sco_pcm_used || !cras_floss_hfp_swb_allowed()) {
    disabled_codecs |= FL_HFP_CODEC_BIT_ID_LC3;
  }
  if (!hfp->sco_pcm_used && !cras_floss_hfp_is_codec_format_supported(
                                hfp, HFP_CODEC_FORMAT_MSBC_TRANSPARENT)) {
    disabled_codecs |= FL_HFP_CODEC_BIT_ID_MSBC;
  }

  rc = floss_media_hfp_start_sco_call(hfp->fm, hfp->addr, hfp->sco_pcm_used,
                                      disabled_codecs);

  if (rc < 0) {
    BTLOG(btlog, BT_SCO_CONNECT, 0, -1);
    return rc;
  }

  if (!(FL_HFP_CODEC_BIT_ID_NONE < rc && rc < FL_HFP_CODEC_BIT_ID_UNKNOWN) ||
      __builtin_popcount(rc) != 1) {
    syslog(LOG_ERR, "Invalid active codec %d", rc);
    BTLOG(btlog, BT_SCO_CONNECT, 0, -1);
    return -EINVAL;
  }

  switch (rc) {
    case FL_HFP_CODEC_BIT_ID_CVSD:
      hfp->active_codec_format = HFP_CODEC_FORMAT_CVSD;
      break;
    case FL_HFP_CODEC_BIT_ID_MSBC:
      hfp->active_codec_format = hfp->sco_pcm_used
                                     ? HFP_CODEC_FORMAT_MSBC
                                     : HFP_CODEC_FORMAT_MSBC_TRANSPARENT;
      break;
    case FL_HFP_CODEC_BIT_ID_LC3:
      hfp->active_codec_format = HFP_CODEC_FORMAT_LC3_TRANSPARENT;
      break;
    default:
      syslog(LOG_ERR, "Invalid active codec format %d", rc);
      BTLOG(btlog, BT_SCO_CONNECT, 0, -1);
      return -EINVAL;
  }

  syslog(LOG_INFO, "Negotiated active codec format is %d",
         hfp->active_codec_format);

  hfp->is_sco_stopped = false;

  if (hfp->sco_pcm_used) {
    // When sco is offloaded, we do not need to connect to the fd in Floss.
    BTLOG(btlog, BT_SCO_CONNECT, 1, -1);
    goto start_dev;
  }

  skt_fd = socket(PF_UNIX, SOCK_STREAM | O_NONBLOCK, 0);
  if (skt_fd < 0) {
    syslog(LOG_WARNING, "Create HFP socket failed with error %d", errno);
    cras_server_metrics_hfp_sco_connection_error(
        CRAS_METRICS_SCO_SKT_OPEN_ERROR);
    rc = skt_fd;
    goto error;
  }

  fill_floss_hfp_skt_addr(&addr);

  syslog(LOG_DEBUG, "Connect to HFP socket at %s ", addr.sun_path);
  rc = connect(skt_fd, (struct sockaddr*)&addr, sizeof(addr));
  if (rc < 0) {
    syslog(LOG_WARNING, "Connect to HFP socket failed with error %d", errno);
    cras_server_metrics_hfp_sco_connection_error(
        CRAS_METRICS_SCO_SKT_CONNECT_ERROR);
    goto error;
  }

  poll_fd.fd = skt_fd;
  poll_fd.events = POLLIN | POLLOUT;

  rc = ppoll(&poll_fd, 1, &timeout, NULL);
  if (rc <= 0) {
    syslog(LOG_WARNING, "Poll for HFP socket failed with error %d", errno);
    cras_server_metrics_hfp_sco_connection_error(
        CRAS_METRICS_SCO_SKT_POLL_TIMEOUT);
    goto error;
  }

  if (poll_fd.revents & (POLLERR | POLLHUP)) {
    syslog(LOG_WARNING, "HFP socket error, revents: %u.", poll_fd.revents);
    cras_server_metrics_hfp_sco_connection_error(
        CRAS_METRICS_SCO_SKT_POLL_ERR_HUP);
    rc = -1;
    goto error;
  }

  hfp->fd = skt_fd;

  audio_thread_add_events_callback(hfp->fd, cb, hfp,
                                   POLLIN | POLLERR | POLLHUP);
  cras_server_metrics_hfp_sco_connection_error(CRAS_METRICS_SCO_SKT_SUCCESS);
  BTLOG(btlog, BT_SCO_CONNECT, 1, hfp->fd);

start_dev:
  set_dev_started(hfp, dir, 1);

  return 0;
error:
  BTLOG(btlog, BT_SCO_CONNECT, 0, skt_fd);
  floss_media_hfp_stop_sco_call(hfp->fm, hfp->addr);
  if (skt_fd >= 0) {
    close(skt_fd);
    unlink(addr.sun_path);
  }
  return rc;
}

int cras_floss_hfp_stop(struct cras_hfp* hfp, enum CRAS_STREAM_DIRECTION dir) {
  // i/odev_started is only used to determine SCO status.
  if (!cras_floss_hfp_is_sco_running(hfp)) {
    return 0;
  }

  set_dev_started(hfp, dir, 0);

  if (cras_floss_hfp_is_sco_running(hfp)) {
    return 0;
  }

  if (hfp->fd >= 0) {
    audio_thread_rm_callback_sync(cras_iodev_list_get_audio_thread(), hfp->fd);
    close(hfp->fd);
  }
  hfp->fd = -1;

  hfp->is_sco_stopped = true;

  BTLOG(btlog, BT_SCO_DISCONNECT, 0, 0);

  return floss_media_hfp_stop_sco_call(hfp->fm, hfp->addr);
}

// This event is where we learn about unsolicited SCO disconnection.
// This can occur at any moment including sensitive timings around
// (before/after) |StopScoCall|, so it is not guaranteed to be triggered
// as a reply to |cras_floss_hfp_stop|.
void cras_floss_hfp_handle_audio_disconnection(struct cras_hfp* hfp) {
  if (hfp->is_sco_stopped) {
    return;
  }

  if (cras_floss_hfp_is_sco_running(hfp)) {
    // Attempt to reconnect to the headset, if and only if:
    // (1) SCO was not requested to be stopped by CRAS, and
    // (2) CRAS is still streaming to HFP
    syslog(LOG_WARNING,
           "HFP audio was disconnected by the headset, attempt to reconnect.");
    cras_bt_policy_switch_profile(hfp->fm->bt_io_mgr);
  }
}

void cras_floss_hfp_set_active(struct cras_hfp* hfp) {
  floss_media_hfp_set_active_device(hfp->fm, hfp->addr);
}

int cras_floss_hfp_get_fd(struct cras_hfp* hfp) {
  return hfp->fd;
}

struct cras_iodev* cras_floss_hfp_get_input_iodev(struct cras_hfp* hfp) {
  return hfp->idev;
}

struct cras_iodev* cras_floss_hfp_get_output_iodev(struct cras_hfp* hfp) {
  return hfp->odev;
}

void cras_floss_hfp_get_iodevs(struct cras_hfp* hfp,
                               struct cras_iodev** idev,
                               struct cras_iodev** odev) {
  *idev = hfp->idev;
  *odev = hfp->odev;
}

const char* cras_floss_hfp_get_display_name(struct cras_hfp* hfp) {
  return hfp->name;
}

const char* cras_floss_hfp_get_addr(struct cras_hfp* hfp) {
  return hfp->addr;
}

const uint32_t cras_floss_hfp_get_stable_id(struct cras_hfp* hfp) {
  char* addr = hfp->addr;
  return SuperFastHash(addr, strlen(addr), strlen(addr));
}

static int convert_hfp_codec_format_to_rate(enum HFP_CODEC_FORMAT codec) {
  switch (codec) {
    case HFP_CODEC_FORMAT_NONE:
      return 0;
    case HFP_CODEC_FORMAT_CVSD:
      return 8000;
    case HFP_CODEC_FORMAT_MSBC_TRANSPARENT:
    case HFP_CODEC_FORMAT_MSBC:
      return 16000;
    case HFP_CODEC_FORMAT_LC3_TRANSPARENT:
      return 32000;
    default:
      syslog(LOG_ERR, "%s: unknown codec format %d", __func__, codec);
      break;
  }
  return 0;
}

int cras_floss_hfp_fill_format(struct cras_hfp* hfp,
                               size_t** rates,
                               snd_pcm_format_t** formats,
                               size_t** channel_counts) {
  *rates = (size_t*)malloc(2 * sizeof(size_t));
  if (!*rates) {
    return -ENOMEM;
  }
  (*rates)[0] = convert_hfp_codec_format_to_rate(hfp->active_codec_format);
  (*rates)[1] = 0;

  *formats = (snd_pcm_format_t*)malloc(2 * sizeof(snd_pcm_format_t));
  if (!*formats) {
    return -ENOMEM;
  }
  (*formats)[0] = SND_PCM_FORMAT_S16_LE;
  (*formats)[1] = (snd_pcm_format_t)0;

  *channel_counts = (size_t*)malloc(2 * sizeof(size_t));
  if (!*channel_counts) {
    return -ENOMEM;
  }
  (*channel_counts)[0] = 1;
  (*channel_counts)[1] = 0;
  return 0;
}

void cras_floss_hfp_set_volume(struct cras_hfp* hfp, unsigned int volume) {
  // Normalize volume value to 0-15
  volume = volume * 15 / 100;
  BTLOG(btlog, BT_HFP_SET_SPEAKER_GAIN, volume, 0);
  floss_media_hfp_set_volume(hfp->fm, volume, hfp->addr);
}

int cras_floss_hfp_convert_volume(unsigned int vgs_volume) {
  if (vgs_volume > 15) {
    syslog(LOG_WARNING, "Illegal VGS volume %u. Adjust to 15", vgs_volume);
    vgs_volume = 15;
  }

  return vgs_volume * 100 / 15;
}

bool cras_floss_hfp_is_sco_running(struct cras_hfp* hfp) {
  return hfp->idev_started || hfp->odev_started;
}

bool cras_floss_hfp_is_codec_format_supported(struct cras_hfp* hfp,
                                              enum HFP_CODEC_FORMAT codec) {
  return hfp->hfp_caps & codec;
}

enum HFP_CODEC_FORMAT cras_floss_hfp_get_active_codec_format(
    struct cras_hfp* hfp) {
  return hfp->active_codec_format;
}

// Destroys given cras_hfp object.
void cras_floss_hfp_destroy(struct cras_hfp* hfp) {
  if (hfp->idev) {
    hfp->sco_pcm_used ? hfp_alsa_iodev_destroy(hfp->idev)
                      : hfp_pcm_iodev_destroy(hfp->idev);
  }
  if (hfp->odev) {
    hfp->sco_pcm_used ? hfp_alsa_iodev_destroy(hfp->odev)
                      : hfp_pcm_iodev_destroy(hfp->odev);
  }
  if (hfp->addr) {
    free(hfp->addr);
  }
  if (hfp->name) {
    free(hfp->name);
  }
  if (hfp->fd >= 0) {
    close(hfp->fd);
  }

  /* Must be the only static connected hfp that we are destroying,
   * so clear it. */
  free(hfp);
}

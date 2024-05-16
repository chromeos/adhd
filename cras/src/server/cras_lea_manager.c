/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // for ppoll
#endif

#include "cras/src/server/cras_lea_manager.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <syslog.h>

#include "cras/server/main_message.h"
#include "cras/src/server/cras_fl_media.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_lea_iodev.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_tm.h"
#include "cras_config.h"
#include "cras_util.h"
#include "third_party/utlist/utlist.h"

#define FLOSS_LEA_DATA_PATH "/run/bluetooth/audio/.lea_data"

/* Object (list) holding information about LE-Audio groups. */
struct lea_group {
  char* name;
  int group_id;
  uint8_t direction;           /* Bitmask of |LEA_AUDIO_DIRECTION| */
  uint16_t available_contexts; /* Bitmask of |LEA_AUDIO_CONTEXT_TYPE| */
  uint32_t data_interval_us;
  uint32_t sample_rate;
  uint8_t bits_per_sample;
  uint8_t channels_count;

  struct cras_iodev *idev, *odev;
  struct lea_group *prev, *next;
};

/* Object holding information and resources of a connected LEA headset. */
struct cras_lea {
  // Object representing the media interface of BT adapter.
  struct fl_media* fm;
  // A list of connected LE-Audio groups.
  // The first group in the list is the primary group.
  struct lea_group* connected_groups;
  // The file descriptor for LEA socket.
  int fd;
  // If the input has started. This is used to determine if
  // a lea start or stop is required.
  int idev_started;
  // If the output has started. This is used to determine if
  // a lea start or stop is required.
  int odev_started;
};

struct cras_iodev* cras_floss_lea_get_primary_odev(struct cras_lea* lea) {
  struct lea_group* group = lea->connected_groups;
  return group ? group->odev : NULL;
}

struct cras_iodev* cras_floss_lea_get_primary_idev(struct cras_lea* lea) {
  struct lea_group* group = lea->connected_groups;
  return group ? group->idev : NULL;
}

bool cras_floss_lea_is_odev_started(struct cras_lea* lea) {
  return lea->odev_started;
}

bool cras_floss_lea_is_idev_started(struct cras_lea* lea) {
  return lea->idev_started;
}

int cras_floss_lea_configure_sink_for_voice_communication(
    struct cras_lea* lea) {
  return floss_media_lea_sink_metadata_changed(
      lea->fm, FL_LEA_AUDIO_SOURCE_VOICE_COMMUNICATION, 1.0);
}

int cras_floss_lea_configure_source_for_voice_communication(
    struct cras_lea* lea) {
  return floss_media_lea_source_metadata_changed(
      lea->fm, FL_LEA_AUDIO_USAGE_VOICE_COMMUNICATION,
      FL_LEA_AUDIO_CONTENT_TYPE_MUSIC, 0.0);
}

int cras_floss_lea_configure_source_for_media(struct cras_lea* lea) {
  return floss_media_lea_source_metadata_changed(
      lea->fm, FL_LEA_AUDIO_USAGE_MEDIA, FL_LEA_AUDIO_CONTENT_TYPE_MUSIC, 1.0);
}

void fill_floss_lea_skt_addr(struct sockaddr_un* addr) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  snprintf(addr->sun_path, CRAS_MAX_SOCKET_PATH_SIZE, FLOSS_LEA_DATA_PATH);
}

static void set_dev_started(struct cras_lea* lea,
                            enum CRAS_STREAM_DIRECTION dir,
                            int started) {
  if (dir == CRAS_STREAM_INPUT) {
    lea->idev_started = started;
  } else if (dir == CRAS_STREAM_OUTPUT) {
    lea->odev_started = started;
  }
}

/* Creates a |cras_lea| object representing the LEA service. */
struct cras_lea* cras_floss_lea_create(struct fl_media* fm) {
  struct cras_lea* lea;
  lea = (struct cras_lea*)calloc(1, sizeof(*lea));

  if (!lea) {
    return NULL;
  }

  lea->fm = fm;
  lea->fd = -1;

  return lea;
}

int cras_floss_lea_start(struct cras_lea* lea,
                         thread_callback cb,
                         enum CRAS_STREAM_DIRECTION dir) {
  int skt_fd;
  int rc;
  struct sockaddr_un addr;
  struct timespec timeout = {10, 0};
  struct pollfd poll_fd;
  struct lea_group* group = lea->connected_groups;

  if ((dir == CRAS_STREAM_INPUT && lea->idev_started) ||
      (dir == CRAS_STREAM_OUTPUT && lea->odev_started)) {
    return -EINVAL;
  }

  if (!group) {
    return -EINVAL;
  }

  if (dir == CRAS_STREAM_INPUT) {
    rc = floss_media_lea_peer_start_audio_request(
        lea->fm, &group->data_interval_us, &group->sample_rate,
        &group->bits_per_sample, &group->channels_count);
  } else if (dir == CRAS_STREAM_OUTPUT) {
    rc = floss_media_lea_host_start_audio_request(
        lea->fm, &group->data_interval_us, &group->sample_rate,
        &group->bits_per_sample, &group->channels_count);
  } else {
    syslog(LOG_ERR, "%s: unsupported direction %d", __func__, dir);
    return -EINVAL;
  }

  if (rc < 0) {
    return rc;
  }

  /* Check if the socket connection has been started by another
   * direction's iodev. We can skip the data channel setup if so. */
  if (lea->idev_started || lea->odev_started) {
    goto start_dev;
  }

  skt_fd = socket(PF_UNIX, SOCK_STREAM | O_NONBLOCK, 0);
  if (skt_fd < 0) {
    syslog(LOG_WARNING, "Create LEA socket failed with error %d", errno);
    rc = skt_fd;
    goto error;
  }

  fill_floss_lea_skt_addr(&addr);

  syslog(LOG_DEBUG, "Connect to LEA socket at %s ", addr.sun_path);
  rc = connect(skt_fd, (struct sockaddr*)&addr, sizeof(addr));
  if (rc < 0) {
    syslog(LOG_WARNING, "Connect to LEA socket failed with error %d", errno);
    goto error;
  }

  poll_fd.fd = skt_fd;
  poll_fd.events = POLLIN | POLLOUT;

  rc = ppoll(&poll_fd, 1, &timeout, NULL);
  if (rc <= 0) {
    syslog(LOG_WARNING, "Poll for LEA socket failed with error %d", errno);
    goto error;
  }

  if (poll_fd.revents & (POLLERR | POLLHUP)) {
    syslog(LOG_WARNING, "LEA socket error, revents: %u.", poll_fd.revents);
    rc = -1;
    goto error;
  }

  lea->fd = skt_fd;

  audio_thread_add_events_callback(lea->fd, cb, lea,
                                   POLLOUT | POLLIN | POLLERR | POLLHUP);

start_dev:
  set_dev_started(lea, dir, 1);

  return 0;
error:
  if (dir == CRAS_STREAM_INPUT) {
    floss_media_lea_peer_stop_audio_request(lea->fm);
  } else if (dir == CRAS_STREAM_OUTPUT) {
    floss_media_lea_host_stop_audio_request(lea->fm);
  }

  if (skt_fd >= 0) {
    close(skt_fd);
    unlink(addr.sun_path);
  }
  return rc;
}

int cras_floss_lea_stop(struct cras_lea* lea, enum CRAS_STREAM_DIRECTION dir) {
  int rc;

  // i/odev_started is only used to determine LEA status.
  if (!(lea->idev_started || lea->odev_started)) {
    return 0;
  }

  set_dev_started(lea, dir, 0);

  if (dir == CRAS_STREAM_INPUT) {
    rc = floss_media_lea_peer_stop_audio_request(lea->fm);
    if (rc < 0) {
      syslog(LOG_ERR, "%s: Failed to stop peer audio request", __func__);
      return rc;
    }
  } else if (dir == CRAS_STREAM_OUTPUT) {
    rc = floss_media_lea_host_stop_audio_request(lea->fm);
    if (rc < 0) {
      syslog(LOG_ERR, "%s: Failed to stop host audio request", __func__);
      return rc;
    }
  }

  if (lea->idev_started || lea->odev_started) {
    return 0;
  }

  if (lea->fd >= 0) {
    audio_thread_rm_callback_sync(cras_iodev_list_get_audio_thread(), lea->fd);
    close(lea->fd);
  }
  lea->fd = -1;

  return 0;
}

int cras_floss_lea_fill_format(struct cras_lea* lea,
                               size_t** rates,
                               snd_pcm_format_t** formats,
                               size_t** channel_counts) {
  struct lea_group* group = lea->connected_groups;

  if (group == NULL) {
    return 0;
  }

  *rates = (size_t*)malloc(2 * sizeof(size_t));
  if (!*rates) {
    return -ENOMEM;
  }
  (*rates)[0] = group->sample_rate;
  (*rates)[1] = 0;

  *formats = (snd_pcm_format_t*)malloc(2 * sizeof(snd_pcm_format_t));
  if (!*formats) {
    return -ENOMEM;
  }
  switch (group->bits_per_sample) {
    case 16:
      (*formats)[0] = SND_PCM_FORMAT_S16_LE;
      break;
    case 24:
      (*formats)[0] = SND_PCM_FORMAT_S24_3LE;
      break;
    case 32:
      (*formats)[0] = SND_PCM_FORMAT_S32_LE;
      break;
    default:
      syslog(LOG_ERR, "%s: Unknown bits_per_sample %d", __func__,
             group->bits_per_sample);
      return -EINVAL;
  }
  (*formats)[1] = (snd_pcm_format_t)0;

  *channel_counts = (size_t*)malloc(2 * sizeof(size_t));
  if (!*channel_counts) {
    return -ENOMEM;
  }
  (*channel_counts)[0] = group->channels_count;
  (*channel_counts)[1] = 0;
  return 0;
}

// TODO: use software volume if VCP is missing.
void cras_floss_lea_set_volume(struct cras_lea* lea, unsigned int volume) {
  syslog(LOG_DEBUG, "%s: set_volume(%u)", __func__, volume);
  struct lea_group* group = lea->connected_groups;
  floss_media_lea_set_group_volume(lea->fm, group->group_id,
                                   volume * 255 / 100);
}

void cras_floss_lea_destroy(struct cras_lea* lea) {
  struct lea_group* group;
  DL_FOREACH (lea->connected_groups, group) {
    if (group->idev) {
      lea_iodev_destroy(group->idev);
    }
    if (group->odev) {
      lea_iodev_destroy(group->odev);
    }
    DL_DELETE(lea->connected_groups, group);
    free(group->name);
    free(group);
  }

  if (lea->fd >= 0) {
    close(lea->fd);
  }

  free(lea);
}

void cras_floss_lea_set_active(struct cras_lea* lea,
                               int group_id,
                               unsigned enabled) {
  // Action is needed (and meaningful) only when there is no stream.
  if (lea->idev_started || lea->odev_started) {
    return;
  }

  if (!enabled) {
    group_id = FL_LEA_GROUP_NONE;
  }

  floss_media_lea_set_active_group(lea->fm, group_id);
}

int cras_floss_lea_get_fd(struct cras_lea* lea) {
  return lea->fd;
}

// TODO: check I/O availability instead of adding both
void cras_floss_lea_add_group(struct cras_lea* lea,
                              const char* name,
                              int group_id) {
  struct lea_group* group;
  DL_FOREACH (lea->connected_groups, group) {
    if (group_id == group->group_id) {
      syslog(LOG_WARNING, "%s: Skipping added group %s", __func__, name);
      return;
    }
  }

  group = (struct lea_group*)calloc(1, sizeof(struct lea_group));
  group->name = strdup(name);
  group->group_id = group_id;

  DL_APPEND(lea->connected_groups, group);

  group->idev = lea_iodev_create(lea, name, group_id, CRAS_STREAM_INPUT);
  group->odev = lea_iodev_create(lea, name, group_id, CRAS_STREAM_OUTPUT);

  // Set plugged and UI will see these iodevs.
  cras_iodev_set_node_plugged(group->idev->active_node, 1);
  cras_iodev_set_node_plugged(group->odev->active_node, 1);

  cras_iodev_list_notify_nodes_changed();
}

void cras_floss_lea_remove_group(struct cras_lea* lea, int group_id) {
  struct lea_group* group;
  DL_FOREACH (lea->connected_groups, group) {
    if (group_id == group->group_id) {
      if (group->idev) {
        cras_iodev_set_node_plugged(group->idev->active_node, 0);
        lea_iodev_destroy(group->idev);
      }

      if (group->odev) {
        cras_iodev_set_node_plugged(group->odev->active_node, 0);
        lea_iodev_destroy(group->odev);
      }

      DL_DELETE(lea->connected_groups, group);
      free(group->name);
      free(group);
    }
  }
}

int cras_floss_lea_audio_conf_updated(struct cras_lea* lea,
                                      uint8_t direction,
                                      int group_id,
                                      uint32_t snk_audio_location,
                                      uint32_t src_audio_location,
                                      uint16_t available_contexts) {
  struct lea_group* group = lea->connected_groups;

  if (!group || group->group_id != group_id) {
    syslog(LOG_WARNING, "Cannot find lea_group %d to update audio conf",
           group_id);
    return -ENOENT;
  }

  group->available_contexts = available_contexts;

  if ((group->direction & LEA_AUDIO_DIRECTION_OUTPUT) !=
      (direction & LEA_AUDIO_DIRECTION_OUTPUT)) {
    if (direction & LEA_AUDIO_DIRECTION_OUTPUT) {
      group->odev->active_node->plugged = 1;
      gettimeofday(&group->odev->active_node->plugged_time, NULL);
    } else {
      cras_iodev_list_disable_and_close_dev_group(group->odev);
    }
  }

  if ((group->direction & LEA_AUDIO_DIRECTION_INPUT) !=
      (direction & LEA_AUDIO_DIRECTION_INPUT)) {
    if (direction & LEA_AUDIO_DIRECTION_INPUT) {
      group->idev->active_node->plugged = 1;
      gettimeofday(&group->idev->active_node->plugged_time, NULL);
    } else {
      cras_iodev_list_disable_and_close_dev_group(group->idev);
    }
  }

  group->direction = direction;

  cras_iodev_list_notify_nodes_changed();

  return 0;
}

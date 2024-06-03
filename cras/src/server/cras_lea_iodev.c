/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>

#include "cras/src/common/byte_buffer.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/audio_thread_log.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_audio_thread_monitor.h"
#include "cras/src/server/cras_bt_policy.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_lea_manager.h"
#include "cras/src/server/cras_utf8.h"
#include "cras_types.h"
#include "cras_util.h"
#include "third_party/superfasthash/sfh.h"
#include "third_party/utlist/utlist.h"

#define PCM_BUF_MAX_SIZE_FRAMES (8192 * 4)
#define PCM_BLOCK_MS 10

#define FLOSS_LEA_MAX_BUF_SIZE_BYTES PCM_BUF_MAX_SIZE_FRAMES * 8

// Child of cras_iodev to handle LEA streaming.
struct lea_io {
  // The cras_iodev structure "base class"
  struct cras_iodev base;
  // Buffer to hold pcm samples.
  struct byte_buffer* pcm_buf;
  // How many frames of audio samples we prefer to write in one
  // socket write.
  unsigned int write_block;
  // The associated |cras_lea| object.
  struct cras_lea* lea;
  // The associated ID of the corresponding LE audio group.
  int group_id;
  // If the device has been configured and attached with any stream.
  int started;

  // TODO: implement presentation delay correctly
  unsigned int bt_stack_delay;
};

static unsigned int bt_local_queued_frames(const struct cras_iodev* iodev) {
  struct lea_io* leaio = (struct lea_io*)iodev;
  if (iodev->format) {
    return buf_queued(leaio->pcm_buf) / cras_get_format_bytes(iodev->format);
  }
  return 0;
}

static int update_supported_formats(struct cras_iodev* iodev) {
  struct lea_io* leaio = (struct lea_io*)iodev;
  free(iodev->supported_channel_counts);
  iodev->supported_channel_counts = NULL;
  free(iodev->supported_rates);
  iodev->supported_rates = NULL;
  free(iodev->supported_formats);
  iodev->supported_formats = NULL;
  int err = cras_floss_lea_fill_format(leaio->lea, &iodev->supported_rates,
                                       &iodev->supported_formats,
                                       &iodev->supported_channel_counts);
  return err;
}

static int lea_write(struct lea_io* odev, size_t target_len) {
  int fd, rc;
  uint8_t* buf;
  unsigned int to_send;

  if (!odev->started) {
    buf_increment_write(odev->pcm_buf, target_len);
  }

  fd = cras_floss_lea_get_fd(odev->lea);

  buf = buf_read_pointer_size(odev->pcm_buf, &to_send);
  while (to_send && target_len) {
    if (to_send > target_len) {
      to_send = target_len;
    }

    rc = send(fd, buf, to_send, MSG_DONTWAIT);
    if (rc <= 0) {
      if (rc < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
        syslog(LOG_WARNING, "Send error %s", cras_strerror(errno));
        return rc;
      }
      syslog(LOG_DEBUG, "rc = %d, errno = %d", rc, errno);
      return 0;
    }
    buf_increment_read(odev->pcm_buf, rc);

    ATLOG(atlog, AUDIO_THREAD_LEA_WRITE, rc, buf_readable(odev->pcm_buf), 0);

    target_len -= rc;
    buf = buf_read_pointer_size(odev->pcm_buf, &to_send);
  }

  return 0;
}

static int frames_queued(const struct cras_iodev* iodev,
                         struct timespec* tstamp) {
  clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
  return bt_local_queued_frames(iodev);
}

static int output_underrun(struct cras_iodev* iodev) {
  unsigned int local_queued_frames = bt_local_queued_frames(iodev);

  /* The upper layer treat underrun in a more strict way. So even
   * this is called it may not be an underrun scenario to LEA audio.
   * Check if local buffer touches zero before trying to fill zero. */
  if (local_queued_frames > 0) {
    return 0;
  }

  // Handle it the same way as cras_iodev_output_underrun().
  return cras_iodev_fill_odev_zeros(iodev, iodev->min_cb_level, true);
}

static int no_stream(struct cras_iodev* iodev, int enable) {
  struct lea_io* leaio = (struct lea_io*)iodev;

  if (iodev->direction != CRAS_STREAM_OUTPUT) {
    return 0;
  }

  // Have output fallback to sending zeros to peer.
  if (enable) {
    leaio->started = 0;
    memset(leaio->pcm_buf->bytes, 0, leaio->pcm_buf->used_size);
  } else {
    leaio->started = 1;
  }
  return 0;
}

static int is_free_running(const struct cras_iodev* iodev) {
  struct lea_io* leaio = (struct lea_io*)iodev;

  if (iodev->direction != CRAS_STREAM_OUTPUT) {
    return 0;
  }

  /* If NOT started, lea_write will automatically puts more data to
   * socket so audio thread doesn't need to wake up for us. */
  return !leaio->started;
}

static int lea_read(struct lea_io* idev) {
  int fd, rc;
  uint8_t* buf;
  unsigned int to_read;

  fd = cras_floss_lea_get_fd(idev->lea);
  // Loop to make sure ring buffer is filled.
  buf = buf_write_pointer_size(idev->pcm_buf, &to_read);
  while (to_read) {
    rc = recv(fd, buf, to_read, MSG_DONTWAIT);
    if (rc <= 0) {
      if (rc < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
        syslog(LOG_WARNING, "Recv error %s", cras_strerror(errno));
        return rc;
      }
      return 0;
    }

    buf_increment_write(idev->pcm_buf, rc);

    ATLOG(atlog, AUDIO_THREAD_LEA_READ, rc, idev->started, 0);

    // Ignore the bytes just read if input dev not in present
    if (!idev->started) {
      buf_increment_read(idev->pcm_buf, rc);
    }

    // Update the to_read and buf pointer
    buf = buf_write_pointer_size(idev->pcm_buf, &to_read);
  }

  return 0;
}

static int lea_socket_read_write_cb(void* arg, int revents) {
  int rc = 0;
  struct cras_lea* lea = (struct cras_lea*)arg;

  struct lea_io* odev = (struct lea_io*)cras_floss_lea_get_primary_odev(lea);
  struct lea_io* idev = (struct lea_io*)cras_floss_lea_get_primary_idev(lea);

  const struct cras_audio_format* fmt = idev->base.format ?: odev->base.format;

  if (!fmt) {
    return 0;
  }

  if (revents & (POLLERR | POLLHUP)) {
    syslog(LOG_WARNING, "Error polling LEA socket, revents %d", revents);
    audio_thread_rm_callback(cras_floss_lea_get_fd(lea));
    // TODO: implement recovery fallback for this case
    return -EPIPE;
  }

  if (revents & POLLIN) {
    lea_read(idev);
  }

  if (revents & POLLOUT) {
    size_t nwrite_btyes = odev->write_block * cras_get_format_bytes(fmt);
    rc = lea_write(odev, nwrite_btyes);
  }

  return rc;
}

static int open_dev(struct cras_iodev* iodev) {
  int rc = 0;
  struct lea_io* leaio = (struct lea_io*)iodev;
  struct cras_lea* lea = leaio->lea;
  struct cras_iodev* odev = cras_floss_lea_get_primary_odev(lea);
  struct cras_iodev* idev = cras_floss_lea_get_primary_idev(lea);

  if (cras_floss_lea_is_context_switching(lea)) {
    return -EAGAIN;
  }

  if (iodev->direction == CRAS_STREAM_INPUT &&
      cras_floss_lea_is_idev_started(lea)) {
    return -EALREADY;
  }

  if (iodev->direction == CRAS_STREAM_OUTPUT &&
      cras_floss_lea_is_odev_started(lea)) {
    return -EALREADY;
  }

  if (odev != iodev && idev != iodev) {
    syslog(LOG_WARNING, "%s: cannot open iodev from a non-primary group",
           __func__);
    return -EINVAL;
  }

  // Immediately apply target context if it is the only direction.
  // Otherwise, either file a context-switch request or acknowledge
  // that the context must already be |CONVERSATIONAL| (the case for output).
  if (iodev->direction == CRAS_STREAM_INPUT) {
    cras_floss_lea_set_target_context(lea, LEA_AUDIO_CONTEXT_CONVERSATIONAL);
    if (!cras_floss_lea_is_odev_started(lea)) {
      cras_floss_lea_apply_target_context(lea);
    } else {
      cras_floss_lea_set_is_context_switching(lea, true);
      cras_bt_policy_lea_switch_context(lea);
      return -EAGAIN;
    }
  } else if (iodev->direction == CRAS_STREAM_OUTPUT) {
    if (!cras_floss_lea_is_idev_started(lea)) {
      cras_floss_lea_set_target_context(lea, LEA_AUDIO_CONTEXT_MEDIA);
      cras_floss_lea_apply_target_context(lea);
    }
  }

  rc = cras_floss_lea_start(lea, lea_socket_read_write_cb, iodev->direction);
  if (rc < 0) {
    syslog(LOG_WARNING, "LEA failed to start for dir %d", iodev->direction);
    return rc;
  }

  return 0;
}

static int configure_dev(struct cras_iodev* iodev) {
  struct lea_io* leaio = (struct lea_io*)iodev;

  // Assert format is set before opening device.
  if (iodev->format == NULL) {
    return -EINVAL;
  }

  iodev->format->format = SND_PCM_FORMAT_S16_LE;
  cras_iodev_init_audio_area(iodev);

  buf_reset(leaio->pcm_buf);
  iodev->buffer_size =
      leaio->pcm_buf->used_size / cras_get_format_bytes(iodev->format);

  leaio->write_block = iodev->format->frame_rate * PCM_BLOCK_MS / 1000;
  leaio->bt_stack_delay = 0;

  iodev->min_buffer_level = 0;
  leaio->started = 1;

  return 0;
}

static int close_dev(struct cras_iodev* iodev) {
  struct lea_io* leaio = (struct lea_io*)iodev;
  struct cras_lea* lea = leaio->lea;

  struct cras_iodev* odev = cras_floss_lea_get_primary_odev(lea);
  struct cras_iodev* idev = cras_floss_lea_get_primary_idev(lea);

  if (odev != iodev && idev != iodev) {
    syslog(LOG_WARNING, "%s: closing an iodev from a non-primary group",
           __func__);
    return -EINVAL;
  }

  if (iodev->direction == CRAS_STREAM_INPUT &&
      cras_floss_lea_is_odev_started(lea) &&
      !cras_floss_lea_is_context_switching(lea)) {
    cras_floss_lea_set_is_context_switching(lea, true);
    cras_floss_lea_set_target_context(lea, LEA_AUDIO_CONTEXT_MEDIA);
    cras_bt_policy_lea_switch_context(lea);
  }

  leaio->started = 0;
  cras_floss_lea_stop(leaio->lea, iodev->direction);

  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    memset(leaio->pcm_buf->bytes, 0, leaio->pcm_buf->used_size);
  }

  cras_iodev_free_format(iodev);
  cras_iodev_free_audio_area(iodev);

  return 0;
}

static int delay_frames(const struct cras_iodev* iodev) {
  const struct lea_io* leaio = (struct lea_io*)iodev;
  struct timespec tstamp;

  /* The number of frames in the pcm buffer plus the delay
   * derived from a2dp_pcm_update_bt_stack_delay. */
  return frames_queued(iodev, &tstamp) + leaio->bt_stack_delay;
}

static int get_buffer(struct cras_iodev* iodev,
                      struct cras_audio_area** area,
                      unsigned* frames) {
  struct lea_io* leaio;
  uint8_t* dst = NULL;
  unsigned buf_avail = 0;
  size_t format_bytes;

  leaio = (struct lea_io*)iodev;

  if (iodev->direction == CRAS_STREAM_OUTPUT && iodev->format) {
    dst = buf_write_pointer_size(leaio->pcm_buf, &buf_avail);
  } else if (iodev->direction == CRAS_STREAM_INPUT && iodev->format) {
    dst = buf_read_pointer_size(leaio->pcm_buf, &buf_avail);
  } else {
    *frames = 0;
    return 0;
  }

  format_bytes = cras_get_format_bytes(iodev->format);

  *frames = MIN(*frames, buf_avail / format_bytes);
  iodev->area->frames = *frames;
  cras_audio_area_config_buf_pointers(iodev->area, iodev->format, dst);

  *area = iodev->area;
  return 0;
}

static int put_buffer(struct cras_iodev* iodev, unsigned frames) {
  struct lea_io* leaio = (struct lea_io*)iodev;
  size_t format_bytes, frames_bytes;

  if (!frames || !iodev->format) {
    return 0;
  }

  format_bytes = cras_get_format_bytes(iodev->format);
  frames_bytes = frames * format_bytes;

  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    if (frames_bytes > buf_writable(leaio->pcm_buf)) {
      return -EINVAL;
    }
    buf_increment_write(leaio->pcm_buf, frames * format_bytes);
  } else if (iodev->direction == CRAS_STREAM_INPUT) {
    if (frames_bytes > buf_readable(leaio->pcm_buf)) {
      return -EINVAL;
    }
    buf_increment_read(leaio->pcm_buf, frames * format_bytes);
  }

  return 0;
}

static int flush_buffer(struct cras_iodev* iodev) {
  struct lea_io* leaio = (struct lea_io*)iodev;
  size_t format_bytes;
  unsigned nframes;

  format_bytes = cras_get_format_bytes(iodev->format);
  if (iodev->direction == CRAS_STREAM_INPUT) {
    nframes = buf_queued(leaio->pcm_buf) / format_bytes;
    buf_increment_read(leaio->pcm_buf, nframes * format_bytes);
  }
  return 0;
}

static void set_volume(struct cras_iodev* iodev) {
  struct lea_io* leaio = (struct lea_io*)iodev;

  cras_floss_lea_set_volume(leaio->lea, iodev->active_node->volume);
}

// This is a critical function that we rely on to synchronize the
// audio context with the BT stack. Ensure that it is safe to call
// multiple times over context switches and on already-enabled devices.
//
// See |lea_context_switch_delay_cb| for potential issues. This is
// currently safe because we ensure that the |target_context| member
// is always updated when we call this function, and delayed calls with
// outdated intentions end up being no-op.
static void update_active_node(struct cras_iodev* iodev,
                               unsigned node_idx,
                               unsigned dev_enabled) {
  struct lea_io* leaio = (struct lea_io*)iodev;
  struct cras_lea* lea = leaio->lea;

  cras_floss_lea_apply_target_context(lea);
}

static void lea_free_base_resources(struct lea_io* leaio) {
  struct cras_ionode* node;
  node = leaio->base.active_node;
  if (node) {
    cras_iodev_rm_node(&leaio->base, node);
    free(node);
  }
  free(leaio->base.supported_channel_counts);
  free(leaio->base.supported_rates);
  free(leaio->base.supported_formats);
}

struct cras_iodev* lea_iodev_create(struct cras_lea* lea,
                                    const char* name,
                                    int group_id,
                                    enum CRAS_STREAM_DIRECTION dir) {
  struct lea_io* leaio;
  struct cras_iodev* iodev;
  struct cras_ionode* node;
  int rc = 0;

  leaio = (struct lea_io*)calloc(1, sizeof(*leaio));
  if (!leaio) {
    return NULL;
  }

  leaio->started = 0;
  leaio->lea = lea;
  leaio->group_id = group_id;

  iodev = &leaio->base;
  iodev->direction = dir;

  iodev->frames_queued = frames_queued;
  iodev->delay_frames = delay_frames;
  iodev->get_buffer = get_buffer;
  iodev->open_dev = open_dev;
  iodev->configure_dev = configure_dev;
  iodev->update_active_node = update_active_node;
  iodev->update_supported_formats = update_supported_formats;
  iodev->put_buffer = put_buffer;
  iodev->flush_buffer = flush_buffer;
  iodev->output_underrun = output_underrun;
  iodev->no_stream = no_stream;
  iodev->close_dev = close_dev;
  iodev->set_volume = set_volume;
  iodev->is_free_running = is_free_running;

  snprintf(iodev->info.name, sizeof(iodev->info.name), "%s group %d", name,
           group_id);
  iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';
  iodev->info.stable_id = SuperFastHash(
      iodev->info.name, strlen(iodev->info.name), strlen(iodev->info.name));

  // Create an empty ionode
  node = (struct cras_ionode*)calloc(1, sizeof(*node));
  node->dev = iodev;
  node->btflags = CRAS_BT_FLAG_FLOSS | CRAS_BT_FLAG_LEA;
  node->type = CRAS_NODE_TYPE_BLUETOOTH;
  node->volume = 100;
  node->stable_id = iodev->info.stable_id;
  strlcpy(node->name, iodev->info.name, sizeof(node->name));
  node->ui_gain_scaler = 1.0f;
  gettimeofday(&node->plugged_time, NULL);

  // The node name exposed to UI should be a valid UTF8 string.
  if (!is_utf8_string(node->name)) {
    strlcpy(node->name, "LEA UTF8 Group Name", sizeof(node->name));
  }

  ewma_power_disable(&iodev->ewma);

  cras_iodev_add_node(iodev, node);

  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    // Expect VC to come later than the group added event, which will
    // update the UI volume along with this attribute.
    iodev->software_volume_needed = 1;
    rc = cras_iodev_list_add(iodev);
  } else if (iodev->direction == CRAS_STREAM_INPUT) {
    rc = cras_iodev_list_add(iodev);
  }
  if (rc) {
    syslog(LOG_ERR, "Add LEA iodev to list err");
    goto error;
  }
  cras_iodev_set_active_node(iodev, node);

  /* We need the buffer to read/write data from/to the LEA group even
   * when there is no corresponding stream. */
  leaio->pcm_buf = byte_buffer_create(FLOSS_LEA_MAX_BUF_SIZE_BYTES);
  if (!leaio->pcm_buf) {
    goto error;
  }

  return iodev;
error:
  lea_free_base_resources(leaio);
  free(leaio);
  return NULL;
}

void lea_iodev_destroy(struct cras_iodev* iodev) {
  int rc;
  struct lea_io* leaio = (struct lea_io*)iodev;

  byte_buffer_destroy(&leaio->pcm_buf);
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    rc = cras_iodev_list_rm(iodev);
  } else if (iodev->direction == CRAS_STREAM_INPUT) {
    rc = cras_iodev_list_rm(iodev);
  } else {
    syslog(LOG_ERR, "%s: Unsupported direction %d", __func__, iodev->direction);
    return;
  }

  if (rc < 0) {
    syslog(LOG_ERR, "%s: Failed to remove iodev, rc=%d", __func__, rc);
    return;
  }

  lea_free_base_resources(leaio);
  cras_iodev_free_resources(iodev);
  free(leaio);
}

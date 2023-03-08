/* Copyright 2021 The ChromiumOS Authors
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
#include "cras/src/server/cras_a2dp_manager.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_audio_thread_monitor.h"
#include "cras/src/server/cras_hfp_manager.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras_types.h"
#include "cras_util.h"
#include "third_party/superfasthash/sfh.h"
#include "third_party/utlist/utlist.h"

#define PCM_BUF_MAX_SIZE_FRAMES (4096 * 4)

/* Floss currently set a 10ms poll interval as A2DP_DATA_READ_POLL_MS.
 * We can't control how the receiving side buffers and consumes data so
 * schedule sending out PCM in blocks corresponding to 10ms to make sure
 * Floss doesn't wait until timeout.
 */
#define PCM_BLOCK_MS 10

/* Schedule the first delay sync 500ms after stream starts, and redo
 * every 10 seconds. */
#define INIT_DELAY_SYNC_MSEC 500
#define DELAY_SYNC_PERIOD_MSEC 10000

/* There's a period of time after streaming starts before BT stack
 * is able to provide non-zero data_position_ts. During this period
 * use a default value for the delay which is supposed to be derived
 * from data_position_ts. */
#define DEFAULT_BT_STACK_DELAY_SEC 0.2f

// Threshold for reasonable a2dp throttle log in audio dump.
static const struct timespec throttle_log_threshold = {
    0, 10000000  // 10ms
};

// Threshold for severe a2dp throttle event.
static const struct timespec throttle_event_threshold = {
    2, 0  // 2s
};

/* The max buffer size. Note that the actual used size must set to multiple
 * of SCO packet size, and the packet size does not necessarily be equal to
 * MTU. We should keep this as common multiple of possible packet sizes, for
 * example: 48, 60, 64, 128.
 */
#define FLOSS_HFP_MAX_BUF_SIZE_BYTES 28800

// Child of cras_iodev to handle bluetooth A2DP streaming.
struct fl_pcm_io {
  // The cras_iodev structure "base class"
  struct cras_iodev base;
  // Buffer to hold pcm samples before encode.
  struct byte_buffer* pcm_buf;
  // The time when it is okay for next flush call.
  struct timespec next_flush_time;
  // The time period between two a2dp packet writes.
  struct timespec flush_period;
  // How many frames of audio samples we prefer to write in one
  // socket write.
  unsigned int write_block;
  // Stores the total audio data in bytes written
  // to BT.
  unsigned long total_written_bytes;
  // Stores the offset of audio data read/write to the BT. This
  // is used to synchronize the read and write data to the BT.
  unsigned long hfp_rw_offset;
  // The timestamp of when last audio data was written to BT.
  struct timespec last_write_ts;
  // The calculated delay in frames from
  // a2dp_pcm_update_bt_stack_delay.
  unsigned int bt_stack_delay;
  // The associated cras_a2dp object.
  struct cras_a2dp* a2dp;
  // The associated cras_hfp object.
  struct cras_hfp* hfp;
  // If the device has been configured and attached with any stream.
  int started;
};

static int flush(const struct cras_iodev* iodev);

static int a2dp_update_supported_formats(struct cras_iodev* iodev) {
  // Supported formats are fixed when iodev created.
  return 0;
}

static int hfp_update_supported_formats(struct cras_iodev* iodev) {
  struct fl_pcm_io* hfpio = (struct fl_pcm_io*)iodev;
  free(iodev->supported_channel_counts);
  iodev->supported_channel_counts = NULL;
  free(iodev->supported_rates);
  iodev->supported_rates = NULL;
  free(iodev->supported_formats);
  iodev->supported_formats = NULL;
  return cras_floss_hfp_fill_format(hfpio->hfp, &iodev->supported_rates,
                                    &iodev->supported_formats,
                                    &iodev->supported_channel_counts);
}

static unsigned int bt_local_queued_frames(const struct cras_iodev* iodev) {
  struct fl_pcm_io* pcmio = (struct fl_pcm_io*)iodev;
  if (iodev->format) {
    return buf_queued(pcmio->pcm_buf) / cras_get_format_bytes(iodev->format);
  }
  return 0;
}

static int frames_queued(const struct cras_iodev* iodev,
                         struct timespec* tstamp) {
  clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
  return bt_local_queued_frames(iodev);
}

/*
 * Utility function to fill zero frames until buffer level reaches
 * target_level. This is useful to allocate just enough data to write
 * to controller, while not introducing extra latency.
 */
static int fill_zeros_to_target_level(struct cras_iodev* iodev,
                                      unsigned int target_level,
                                      bool underrun) {
  unsigned int local_queued_frames = bt_local_queued_frames(iodev);

  if (local_queued_frames < target_level) {
    return cras_iodev_fill_odev_zeros(iodev, target_level - local_queued_frames,
                                      underrun);
  }
  return 0;
}

/*
 * dev_io_playback_write() has the logic to detect underrun scenario
 * and calls into this underrun ops, by comparing buffer level with
 * number of frames just written. Note that it's not correct 100% of
 * the time in a2dp case, because we lose track of samples once they're
 * flushed to socket.
 */
static int a2dp_output_underrun(struct cras_iodev* iodev) {
  unsigned int local_queued_frames = bt_local_queued_frames(iodev);

  /*
   * Examples to help understand the check:
   *
   * [False-positive underrun]
   * Assume min_buffer_level = 1000, written 900, and flushes
   * 800 of data. Audio thread sees 1000 + 900 - 800 = 1100 of
   * data left. This is merely 100(< 900) above min_buffer_level
   * so audio_thread thinks it underruns, but actually not.
   *
   * [True underrun]
   * min_buffer_level = 1000, written 200, and flushes 800 of
   * data. Now that buffer runs lower than min_buffer_level so
   * it's indeed an underrun.
   */
  if (local_queued_frames > iodev->min_buffer_level) {
    return 0;
  }

  // Make sure the hw_level doesn't underrun after one flush.
  return fill_zeros_to_target_level(iodev, 2 * iodev->min_buffer_level, true);
}

/*
 * This will be called multiple times when a2dpio is in no_stream state
 * frames_to_play_in_sleep ops determines how regular this will be called.
 */
static int a2dp_enter_no_stream(struct cras_iodev* odev) {
  int rc;
  /* Setting target level to 3 times of min_buffer_level.
   * We want hw_level to stay bewteen 1-2 times of min_buffer_level on top
   * of the underrun threshold(i.e one min_cb_level). */
  rc = fill_zeros_to_target_level(odev, 3 * odev->min_buffer_level, false);
  if (rc) {
    syslog(LOG_WARNING, "Error in A2DP enter_no_stream");
  }
  return flush(odev);
}

/*
 * This is called when stream data is available to write. Prepare audio
 * data to one write_block. Don't flush it now because stream data is
 * coming right up which will trigger next flush at appropriate time.
 */
static int a2dp_leave_no_stream(struct cras_iodev* odev) {
  /* Since stream data is ready, just make sure hw_level doesn't underrun
   * after one flush. Hence setting the target level to 2 times of
   * min_buffer_level. */
  return fill_zeros_to_target_level(odev, 2 * odev->min_buffer_level, false);
}

/*
 * Makes sure there's enough data(zero frames) to flush when no stream presents.
 * Note that the underrun condition is when real buffer level goes below
 * min_buffer_level, so we want to keep data at a reasonable higher level on top
 * of that.
 */
static int a2dp_no_stream(struct cras_iodev* odev, int enable) {
  if (enable) {
    return a2dp_enter_no_stream(odev);
  } else {
    return a2dp_leave_no_stream(odev);
  }
}

static int hfp_output_underrun(struct cras_iodev* iodev) {
  unsigned int local_queued_frames = bt_local_queued_frames(iodev);

  /* The upper layer treat underrun in a more strict way. So even
   * this is called it may not be an underrun scenario to HFP audio.
   * Check if local buffer touches zero before trying to fill zero. */
  if (local_queued_frames > 0) {
    return 0;
  }

  // Handle it the same way as cras_iodev_output_underrun().
  return cras_iodev_fill_odev_zeros(iodev, iodev->min_cb_level, true);
}

static int hfp_no_stream(struct cras_iodev* iodev, int enable) {
  struct fl_pcm_io* hfpio = (struct fl_pcm_io*)iodev;

  if (iodev->direction != CRAS_STREAM_OUTPUT) {
    return 0;
  }

  // Have output fallback to sending zeros to HF.
  if (enable) {
    hfpio->started = 0;
    memset(hfpio->pcm_buf->bytes, 0, hfpio->pcm_buf->used_size);
  } else {
    hfpio->started = 1;
  }
  return 0;
}

static int hfp_is_free_running(const struct cras_iodev* iodev) {
  struct fl_pcm_io* hfpio = (struct fl_pcm_io*)iodev;

  if (iodev->direction != CRAS_STREAM_OUTPUT) {
    return 0;
  }

  /* If NOT started, hfp_write will automatically puts more data to
   * socket so audio thread doesn't need to wake up for us. */
  return !hfpio->started;
}

/*
 * To be called when PCM socket becomes writable.
 */
static int a2dp_socket_write_cb(void* arg, int revent) {
  struct cras_iodev* iodev = (struct cras_iodev*)arg;
  return flush(iodev);
}

static int a2dp_configure_dev(struct cras_iodev* iodev) {
  struct fl_pcm_io* a2dpio = (struct fl_pcm_io*)iodev;
  int rc, fd, init_level;
  size_t format_bytes;
  uint8_t* buf;
  unsigned int to_send;

  rc = cras_floss_a2dp_start(a2dpio->a2dp, iodev->format);
  if (rc < 0) {
    syslog(LOG_WARNING, "A2dp start failed");
    return rc;
  }

  // Assert format is set before opening device.
  if (iodev->format == NULL) {
    return -EINVAL;
  }
  iodev->format->format = SND_PCM_FORMAT_S16_LE;
  format_bytes = cras_get_format_bytes(iodev->format);
  cras_iodev_init_audio_area(iodev, iodev->format->num_channels);

  a2dpio->total_written_bytes = 0;
  a2dpio->bt_stack_delay = 0;

  /* Configure write_block to frames equivalent to PCM_BLOCK_MS.
   * And make buffer_size integer multiple of write_block so we
   * don't get cut easily in ring buffer. */
  a2dpio->write_block = iodev->format->frame_rate * PCM_BLOCK_MS / 1000;
  iodev->buffer_size =
      PCM_BUF_MAX_SIZE_FRAMES / a2dpio->write_block * a2dpio->write_block;

  a2dpio->pcm_buf = byte_buffer_create(iodev->buffer_size * format_bytes);
  if (!a2dpio->pcm_buf) {
    return -ENOMEM;
  }

  /* Initialize flush_period by write_block, it will be changed
   * later based on socket write schedule. */
  cras_frames_to_time(a2dpio->write_block, iodev->format->frame_rate,
                      &a2dpio->flush_period);

  /* Buffer level less than one preferable write_block to be sent in one
   * socket write. Configure min_buffer_level to this value so when stream
   * underruns, audio thread can take action to fill some zeros. */
  iodev->min_buffer_level = a2dpio->write_block;

  fd = cras_floss_a2dp_get_fd(a2dpio->a2dp);
  audio_thread_add_events_callback(fd, a2dp_socket_write_cb, iodev,
                                   POLLOUT | POLLERR | POLLHUP);
  audio_thread_config_events_callback(fd, TRIGGER_NONE);

  /* Send one block of silence to Floss as jitter buffer to handle the
   * variation in packet scheduling caused by clock drift and state-polling. */
  cras_iodev_fill_odev_zeros(iodev, a2dpio->write_block, false);
  init_level = a2dpio->write_block * cras_get_format_bytes(iodev->format);

  buf = buf_read_pointer_size(a2dpio->pcm_buf, &to_send);
  while (to_send && init_level > 0) {
    if (to_send > init_level) {
      to_send = init_level;
    }
    rc = send(fd, buf, to_send, MSG_DONTWAIT);
    if (rc <= 0) {
      break;
    }
    buf_increment_read(a2dpio->pcm_buf, rc);
    init_level -= rc;
    buf = buf_read_pointer_size(a2dpio->pcm_buf, &to_send);
  }
  if (init_level) {
    syslog(
        LOG_WARNING,
        "Failed to send all init buffer, left %d bytes, to_send = %u, rc = %d",
        init_level, to_send, rc);
  }

  clock_gettime(CLOCK_MONOTONIC_RAW, &a2dpio->next_flush_time);
  cras_floss_a2dp_delay_sync(a2dpio->a2dp, INIT_DELAY_SYNC_MSEC,
                             DELAY_SYNC_PERIOD_MSEC);
  return 0;
}

static int hfp_read(struct fl_pcm_io* idev) {
  int fd, rc;
  uint8_t* buf;
  unsigned int to_read;

  fd = cras_floss_hfp_get_fd(idev->hfp);
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

    // Ignore the bytes just read if input dev not in present
    if (!idev->started) {
      buf_increment_read(idev->pcm_buf, rc);
    }

    idev->hfp_rw_offset += rc;

    // Update the to_read and buf pointer
    buf = buf_write_pointer_size(idev->pcm_buf, &to_read);
  }
  return 0;
}

static int hfp_write(struct fl_pcm_io* odev, size_t target_len) {
  int fd, rc;
  uint8_t* buf;
  unsigned int to_send;

  /* Without output stream's presence, we shall still send zero packets
   * to HF. This is required for some HF devices to start sending non-zero
   * data to AG. */
  if (!odev->started) {
    buf_increment_write(odev->pcm_buf, target_len);
  }

  fd = cras_floss_hfp_get_fd(odev->hfp);

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
      return 0;
    }
    buf_increment_read(odev->pcm_buf, rc);

    odev->hfp_rw_offset += rc;
    target_len -= rc;
    buf = buf_read_pointer_size(odev->pcm_buf, &to_send);
  }
  return 0;
}

static int hfp_socket_read_write_cb(void* arg, int revents) {
  int rc;
  struct cras_hfp* hfp = (struct cras_hfp*)arg;
  struct fl_pcm_io *idev, *odev;
  size_t nwrite_btyes;

  idev = (struct fl_pcm_io*)cras_floss_hfp_get_input_iodev(hfp);
  odev = (struct fl_pcm_io*)cras_floss_hfp_get_output_iodev(hfp);

  const struct cras_audio_format* fmt = idev->base.format ?: odev->base.format;

  if (!fmt) {
    return 0;
  }

  // Allow last read before handling error or hang-up events.
  if (revents & POLLIN) {
    rc = hfp_read(idev);
    if (rc) {
      return rc;
    }
  }
  if (revents & (POLLERR | POLLHUP)) {
    syslog(LOG_WARNING, "Error polling SCO socket, revents %d", revents);
    if (revents & POLLHUP) {
      syslog(LOG_INFO,
             "Received POLLHUP, remove callback and wait for reconnection.");
      idev->started = 0;
      odev->started = 0;
      // Leave hfp->fd for hfp_manager to cleanup.
      audio_thread_rm_callback(cras_floss_hfp_get_fd(hfp));
    }
    return -EPIPE;
  }

  nwrite_btyes = odev->write_block * cras_get_format_bytes(fmt);
  rc = hfp_write(odev, idev->hfp_rw_offset > odev->hfp_rw_offset
                           ? idev->hfp_rw_offset - odev->hfp_rw_offset
                           : nwrite_btyes);
  if (idev->hfp_rw_offset == odev->hfp_rw_offset) {
    idev->hfp_rw_offset = odev->hfp_rw_offset = 0;
  }

  return rc;
}

static int hfp_open_dev(struct cras_iodev* iodev) {
  struct fl_pcm_io* hfpio = (struct fl_pcm_io*)iodev;
  int rc;

  rc = cras_floss_hfp_start(hfpio->hfp, hfp_socket_read_write_cb,
                            iodev->direction);
  if (rc < 0) {
    syslog(LOG_WARNING, "HFP failed to start");
    return rc;
  }

  if (iodev->direction == CRAS_STREAM_INPUT &&
      !cras_floss_hfp_get_wbs_supported(hfpio->hfp)) {
    iodev->active_node->type = CRAS_NODE_TYPE_BLUETOOTH_NB_MIC;
  }

  return 0;
}

static int hfp_configure_dev(struct cras_iodev* iodev) {
  struct fl_pcm_io* hfpio = (struct fl_pcm_io*)iodev;

  // Assert format is set before opening device.
  if (iodev->format == NULL) {
    return -EINVAL;
  }
  iodev->format->format = SND_PCM_FORMAT_S16_LE;
  cras_iodev_init_audio_area(iodev, iodev->format->num_channels);

  buf_reset(hfpio->pcm_buf);
  iodev->buffer_size =
      hfpio->pcm_buf->used_size / cras_get_format_bytes(iodev->format);

  hfpio->write_block = iodev->format->frame_rate * PCM_BLOCK_MS / 1000;
  hfpio->bt_stack_delay = 0;

  // As we directly write PCM here, there is no min buffer limitation.
  iodev->min_buffer_level = 0;

  hfpio->started = 1;

  return 0;
}

static int a2dp_close_dev(struct cras_iodev* iodev) {
  struct fl_pcm_io* a2dpio = (struct fl_pcm_io*)iodev;
  int fd;

  fd = cras_floss_a2dp_get_fd(a2dpio->a2dp);

  if (fd >= 0) {
    audio_thread_rm_callback_sync(cras_iodev_list_get_audio_thread(), fd);
  }

  cras_floss_a2dp_stop(a2dpio->a2dp);

  cras_floss_a2dp_cancel_suspend(a2dpio->a2dp);
  byte_buffer_destroy(&a2dpio->pcm_buf);
  cras_iodev_free_format(iodev);
  cras_iodev_free_audio_area(iodev);
  return 0;
}

static int hfp_close_dev(struct cras_iodev* iodev) {
  struct fl_pcm_io* hfpio = (struct fl_pcm_io*)iodev;

  hfpio->started = 0;
  cras_floss_hfp_stop(hfpio->hfp, iodev->direction);

  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    memset(hfpio->pcm_buf->bytes, 0, hfpio->pcm_buf->used_size);
  }

  cras_iodev_free_format(iodev);
  cras_iodev_free_audio_area(iodev);
  return 0;
}

static unsigned int a2dp_frames_to_play_in_sleep(struct cras_iodev* iodev,
                                                 unsigned int* hw_level,
                                                 struct timespec* hw_tstamp) {
  struct fl_pcm_io* a2dpio = (struct fl_pcm_io*)iodev;
  int frames_until;

  *hw_level = frames_queued(iodev, hw_tstamp);

  frames_until = cras_frames_until_time(&a2dpio->next_flush_time,
                                        iodev->format->frame_rate);
  if (frames_until > 0) {
    return frames_until;
  }

  /* If time has passed next_flush_time, for example when socket write
   * throttles, sleep a moderate of time so that audio thread doesn't
   * busy wake up. */
  return a2dpio->write_block;
}

/* Flush PCM data to the socket.
 * Returns:
 *    0 when the flush succeeded, -1 when error occurred.
 */
static int flush(const struct cras_iodev* iodev) {
  int written = 0;
  int fd;
  unsigned int queued_frames;
  size_t format_bytes;
  struct fl_pcm_io* a2dpio;
  struct timespec now, ts;
  static const struct timespec flush_wake_fuzz_ts = {
      0, 1000000  // 1ms
  };

  a2dpio = (struct fl_pcm_io*)iodev;

  ATLOG(atlog, AUDIO_THREAD_A2DP_FLUSH, iodev->state,
        a2dpio->next_flush_time.tv_sec, a2dpio->next_flush_time.tv_nsec);
  // Only allow data to be flushed after start() ops is called.
  if ((iodev->state != CRAS_IODEV_STATE_NORMAL_RUN) &&
      (iodev->state != CRAS_IODEV_STATE_NO_STREAM_RUN)) {
    return 0;
  }

  fd = cras_floss_a2dp_get_fd(a2dpio->a2dp);
do_flush:
  // If flush gets called before targeted next flush time, do nothing.
  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  add_timespecs(&now, &flush_wake_fuzz_ts);
  if (!timespec_after(&now, &a2dpio->next_flush_time)) {
    if (iodev->buffer_size == bt_local_queued_frames(iodev)) {
      /*
       * If buffer is full, audio thread will no longer call
       * into get/put buffer in subsequent wake-ups. In that
       * case set the registered callback to be triggered at
       * next audio thread wake up.
       */
      audio_thread_config_events_callback(fd, TRIGGER_WAKEUP);
      cras_audio_thread_event_a2dp_overrun();
      syslog(LOG_WARNING, "Buffer overrun in A2DP pcm iodev");
    }
    return 0;
  }
  /* If the A2DP write schedule miss exceeds a small threshold, log it for
   * debug purpose. */
  subtract_timespecs(&now, &a2dpio->next_flush_time, &ts);
  if (timespec_after(&ts, &throttle_log_threshold)) {
    ATLOG(atlog, AUDIO_THREAD_A2DP_THROTTLE_TIME, ts.tv_sec, ts.tv_nsec,
          bt_local_queued_frames(iodev));
  }

  /* Log an event if the A2DP write schedule miss exceeds a large
   * threshold that we consider it as something severe. */
  if (timespec_after(&ts, &throttle_event_threshold)) {
    cras_audio_thread_event_a2dp_throttle();
  }

  format_bytes = cras_get_format_bytes(iodev->format);
  if (bt_local_queued_frames(iodev) >= a2dpio->write_block) {
    written = send(fd, buf_read_pointer(a2dpio->pcm_buf),
                   a2dpio->write_block * format_bytes, MSG_DONTWAIT);
  }

  ATLOG(atlog, AUDIO_THREAD_A2DP_WRITE, written / format_bytes,
        buf_readable(a2dpio->pcm_buf), 0);

  if (written < 0) {
    // Track one failure because of EAGAIN error.
    cras_floss_a2dp_update_write_status(a2dpio->a2dp, false);
    if (errno == EAGAIN) {
      /* If EAGAIN error lasts longer than 5 seconds, suspend
       * the a2dp connection. */
      cras_floss_a2dp_schedule_suspend(a2dpio->a2dp, 5000,
                                       A2DP_EXIT_LONG_TX_FAILURE);
      audio_thread_config_events_callback(fd, TRIGGER_WAKEUP);
      return 0;
    } else {
      cras_floss_a2dp_cancel_suspend(a2dpio->a2dp);
      /* ECONNRESET is a common error when the remote headset
       * initiates disconnection so separate it from other
       * rarely happened errors. */
      cras_floss_a2dp_schedule_suspend(a2dpio->a2dp, 0,
                                       written == -ECONNRESET
                                           ? A2DP_EXIT_CONN_RESET
                                           : A2DP_EXIT_TX_FATAL_ERROR);

      syslog(LOG_ERR, "A2DP socket write error %d", written);

      audio_thread_config_events_callback(fd, TRIGGER_NONE);
      return written;
    }
  }

  if (written > 0) {
    /* Adds some time to next_flush_time according to how many
     * frames just written to socket. */
    cras_frames_to_time(written / format_bytes, iodev->format->frame_rate,
                        &a2dpio->flush_period);
    add_timespecs(&a2dpio->next_flush_time, &a2dpio->flush_period);
    buf_increment_read(a2dpio->pcm_buf, written);
    a2dpio->total_written_bytes += written;
    a2dpio->last_write_ts = now;
    // Track success because frames got written.
    cras_floss_a2dp_update_write_status(a2dpio->a2dp, true);
  }

  /* a2dp_write no longer return -EAGAIN when reaches here, disable
   * the polling write callback. */
  audio_thread_config_events_callback(fd, TRIGGER_NONE);

  cras_floss_a2dp_cancel_suspend(a2dpio->a2dp);

  /* If it looks okay to write more and we do have queued data, try
   * to write more.
   */
  queued_frames = buf_queued(a2dpio->pcm_buf) / format_bytes;
  if (written &&
      (queued_frames >= a2dpio->write_block + iodev->min_buffer_level)) {
    goto do_flush;
  }

  return 0;
}

static int delay_frames(const struct cras_iodev* iodev) {
  const struct fl_pcm_io* pcmio = (struct fl_pcm_io*)iodev;
  struct timespec tstamp;

  /* The number of frames in the pcm buffer plus the delay
   * derived from a2dp_pcm_update_bt_stack_delay. */
  return frames_queued(iodev, &tstamp) + pcmio->bt_stack_delay;
}

static int get_buffer(struct cras_iodev* iodev,
                      struct cras_audio_area** area,
                      unsigned* frames) {
  struct fl_pcm_io* pcmio;
  uint8_t* dst = NULL;
  unsigned buf_avail = 0;
  size_t format_bytes;

  pcmio = (struct fl_pcm_io*)iodev;

  if (iodev->direction == CRAS_STREAM_OUTPUT && iodev->format) {
    dst = buf_write_pointer_size(pcmio->pcm_buf, &buf_avail);
  } else if (iodev->direction == CRAS_STREAM_INPUT && iodev->format) {
    dst = buf_read_pointer_size(pcmio->pcm_buf, &buf_avail);
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

static int a2dp_put_buffer(struct cras_iodev* iodev, unsigned nwritten) {
  size_t written_bytes;
  size_t format_bytes;
  struct fl_pcm_io* a2dpio = (struct fl_pcm_io*)iodev;

  format_bytes = cras_get_format_bytes(iodev->format);
  written_bytes = nwritten * format_bytes;

  if (written_bytes > buf_writable(a2dpio->pcm_buf)) {
    return -EINVAL;
  }

  buf_increment_write(a2dpio->pcm_buf, written_bytes);

  return flush(iodev);
}

static int hfp_put_buffer(struct cras_iodev* iodev, unsigned frames) {
  struct fl_pcm_io* pcmio = (struct fl_pcm_io*)iodev;
  size_t format_bytes, frames_bytes;

  if (!frames || !iodev->format) {
    return 0;
  }

  format_bytes = cras_get_format_bytes(iodev->format);
  frames_bytes = frames * format_bytes;

  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    if (frames_bytes > buf_writable(pcmio->pcm_buf)) {
      return -EINVAL;
    }
    buf_increment_write(pcmio->pcm_buf, frames * format_bytes);
  } else if (iodev->direction == CRAS_STREAM_INPUT) {
    if (frames_bytes > buf_readable(pcmio->pcm_buf)) {
      return -EINVAL;
    }
    buf_increment_read(pcmio->pcm_buf, frames * format_bytes);
  }

  return 0;
}

static int a2dp_flush_buffer(struct cras_iodev* iodev) {
  return 0;
}

static int hfp_flush_buffer(struct cras_iodev* iodev) {
  struct fl_pcm_io* pcmio = (struct fl_pcm_io*)iodev;
  size_t format_bytes;
  unsigned nframes;

  format_bytes = cras_get_format_bytes(iodev->format);
  if (iodev->direction == CRAS_STREAM_INPUT) {
    nframes = buf_queued(pcmio->pcm_buf) / format_bytes;
    buf_increment_read(pcmio->pcm_buf, nframes * format_bytes);
  }
  return 0;
}

static void a2dp_set_volume(struct cras_iodev* iodev) {
  struct fl_pcm_io* a2dpio = (struct fl_pcm_io*)iodev;

  if (iodev->software_volume_needed) {
    return;
  }

  cras_floss_a2dp_set_volume(a2dpio->a2dp, iodev->active_node->volume);
}

static void hfp_set_volume(struct cras_iodev* iodev) {
  struct fl_pcm_io* hfpio = (struct fl_pcm_io*)iodev;

  cras_floss_hfp_set_volume(hfpio->hfp, iodev->active_node->volume);
}

static void a2dp_update_active_node(struct cras_iodev* iodev,
                                    unsigned node_idx,
                                    unsigned dev_enabled) {
  struct fl_pcm_io* a2dpio = (struct fl_pcm_io*)iodev;
  cras_floss_a2dp_set_active(a2dpio->a2dp, dev_enabled);
}

static void hfp_update_active_node(struct cras_iodev* iodev,
                                   unsigned node_idx,
                                   unsigned dev_enabled) {}

void pcm_free_base_resources(struct fl_pcm_io* pcmio) {
  struct cras_ionode* node;

  node = pcmio->base.active_node;
  if (node) {
    cras_iodev_rm_node(&pcmio->base, node);
    free(node);
  }
  free(pcmio->base.supported_channel_counts);
  free(pcmio->base.supported_rates);
  free(pcmio->base.supported_formats);
}

struct fl_pcm_io* pcm_iodev_create(enum CRAS_STREAM_DIRECTION dir,
                                   const char* name,
                                   const char* addr) {
  struct fl_pcm_io* pcmio;
  struct cras_iodev* iodev;
  struct cras_ionode* node;
  pcmio = (struct fl_pcm_io*)calloc(1, sizeof(*pcmio));
  if (!pcmio) {
    return NULL;
  }

  iodev = &pcmio->base;
  iodev->direction = dir;

  snprintf(iodev->info.name, sizeof(iodev->info.name), "%s", name);
  iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';

  // Address determines the unique stable id.
  iodev->info.stable_id = SuperFastHash(addr, strlen(addr), strlen(addr));

  // Same callbacks for A2DP and HFP
  iodev->frames_queued = frames_queued;
  iodev->delay_frames = delay_frames;
  iodev->get_buffer = get_buffer;

  // A2DP specific fields
  iodev->start = NULL;
  iodev->frames_to_play_in_sleep = NULL;
  iodev->output_underrun = NULL;

  // HFP specific fields
  iodev->is_free_running = NULL;

  // Create an empty ionode
  node = (struct cras_ionode*)calloc(1, sizeof(*node));
  node->dev = iodev;
  strlcpy(node->name, iodev->info.name, sizeof(node->name));
  node->type = CRAS_NODE_TYPE_BLUETOOTH;
  node->volume = 100;
  gettimeofday(&node->plugged_time, NULL);
  node->btflags |= CRAS_BT_FLAG_FLOSS;

  cras_iodev_add_node(iodev, node);
  cras_iodev_set_active_node(iodev, node);

  ewma_power_disable(&iodev->ewma);
  return pcmio;
}

static void set_a2dp_callbacks(struct cras_iodev* a2dpio) {
  a2dpio->configure_dev = a2dp_configure_dev;
  a2dpio->update_active_node = a2dp_update_active_node;
  a2dpio->update_supported_formats = a2dp_update_supported_formats;
  a2dpio->put_buffer = a2dp_put_buffer;
  a2dpio->flush_buffer = a2dp_flush_buffer;
  a2dpio->no_stream = a2dp_no_stream;
  a2dpio->close_dev = a2dp_close_dev;
  a2dpio->set_volume = a2dp_set_volume;

  a2dpio->frames_to_play_in_sleep = a2dp_frames_to_play_in_sleep;
  a2dpio->output_underrun = a2dp_output_underrun;
}

struct cras_iodev* a2dp_pcm_iodev_create(struct cras_a2dp* a2dp,
                                         int sample_rate,
                                         int bits_per_sample,
                                         int channel_mode) {
  int err;
  struct fl_pcm_io* a2dpio;
  struct cras_iodev* iodev;

  // A2DP only does output now
  a2dpio = pcm_iodev_create(CRAS_STREAM_OUTPUT,
                            cras_floss_a2dp_get_display_name(a2dp),
                            cras_floss_a2dp_get_addr(a2dp));
  syslog(LOG_DEBUG, "a2dpio_create = %p.", a2dpio);
  if (!a2dpio) {
    return NULL;
  }

  iodev = &a2dpio->base;
  a2dpio->a2dp = a2dp;

  err = cras_floss_a2dp_fill_format(
      sample_rate, bits_per_sample, channel_mode, &iodev->supported_rates,
      &iodev->supported_formats, &iodev->supported_channel_counts);
  if (err) {
    goto error;
  }

  iodev->active_node->btflags |= CRAS_BT_FLAG_A2DP;
  set_a2dp_callbacks(iodev);
  return iodev;
error:
  pcm_free_base_resources(a2dpio);
  free(a2dpio);
  return NULL;
}

void a2dp_pcm_iodev_destroy(struct cras_iodev* iodev) {
  struct fl_pcm_io* a2dpio = (struct fl_pcm_io*)iodev;

  // Free resources when device successfully removed.
  cras_iodev_list_rm_output(iodev);
  cras_iodev_free_resources(iodev);
  pcm_free_base_resources(a2dpio);
  free(a2dpio);
}

void a2dp_pcm_update_bt_stack_delay(struct cras_iodev* iodev,
                                    uint64_t remote_delay_report_ns,
                                    uint64_t total_bytes_read,
                                    struct timespec* data_position_ts) {
  struct fl_pcm_io* a2dpio = (struct fl_pcm_io*)iodev;
  size_t format_bytes = cras_get_format_bytes(iodev->format);
  struct timespec diff;
  unsigned int delay;

  /* The BT stack delay is composed by two parts: the delay from remote
   * headset, and the delay from local BT stack. */
  diff.tv_sec = 0;
  diff.tv_nsec = remote_delay_report_ns;
  while (diff.tv_nsec >= 1000000000) {
    diff.tv_nsec -= 1000000000;
    diff.tv_sec += 1;
  }
  delay = cras_time_to_frames(&diff, iodev->format->frame_rate);

  /* Local BT stack delay is calculated based on the formula
   * (N1 - N0) + rate * (T1 - T0). */
  if (!data_position_ts->tv_sec && !data_position_ts->tv_nsec) {
    delay += iodev->format->frame_rate * DEFAULT_BT_STACK_DELAY_SEC;
  } else if (timespec_after(data_position_ts, &a2dpio->last_write_ts)) {
    subtract_timespecs(data_position_ts, &a2dpio->last_write_ts, &diff);
    delay += (a2dpio->total_written_bytes - total_bytes_read) / format_bytes +
             cras_time_to_frames(&diff, iodev->format->frame_rate);
  } else {
    subtract_timespecs(&a2dpio->last_write_ts, data_position_ts, &diff);
    delay += (a2dpio->total_written_bytes - total_bytes_read) / format_bytes -
             cras_time_to_frames(&diff, iodev->format->frame_rate);
  }
  a2dpio->bt_stack_delay = delay;

  syslog(LOG_DEBUG, "Update: bt_stack_delay %u", a2dpio->bt_stack_delay);
}

static void set_hfp_callbacks(struct cras_iodev* hfpio) {
  hfpio->open_dev = hfp_open_dev;
  hfpio->configure_dev = hfp_configure_dev;
  hfpio->update_active_node = hfp_update_active_node;
  hfpio->update_supported_formats = hfp_update_supported_formats;
  hfpio->put_buffer = hfp_put_buffer;
  hfpio->flush_buffer = hfp_flush_buffer;
  hfpio->output_underrun = hfp_output_underrun;
  hfpio->no_stream = hfp_no_stream;
  hfpio->close_dev = hfp_close_dev;
  hfpio->set_volume = hfp_set_volume;

  hfpio->is_free_running = hfp_is_free_running;
}

struct cras_iodev* hfp_pcm_iodev_create(struct cras_hfp* hfp,
                                        enum CRAS_STREAM_DIRECTION dir) {
  struct fl_pcm_io* hfpio;
  struct cras_iodev* iodev;
  int err;

  hfpio = pcm_iodev_create(dir, cras_floss_hfp_get_display_name(hfp),
                           cras_floss_hfp_get_addr(hfp));
  if (!hfpio) {
    return NULL;
  }

  iodev = &hfpio->base;

  hfpio->started = 0;
  hfpio->hfp = hfp;

  err = cras_floss_hfp_fill_format(hfp, &iodev->supported_rates,
                                   &iodev->supported_formats,
                                   &iodev->supported_channel_counts);
  if (err) {
    goto error;
  }

  // Record max supported channels into cras_iodev_info.
  iodev->info.max_supported_channels = 1;

  /* We need the buffer to read/write data from/to the HFP device even
   * when there is no corresponding stream. */
  hfpio->pcm_buf = byte_buffer_create(FLOSS_HFP_MAX_BUF_SIZE_BYTES);
  if (!hfpio->pcm_buf) {
    goto error;
  }

  if (iodev->direction == CRAS_STREAM_INPUT &&
      !cras_floss_hfp_get_wbs_supported(hfp)) {
    iodev->active_node->type = CRAS_NODE_TYPE_BLUETOOTH_NB_MIC;
  }

  iodev->active_node->btflags |= CRAS_BT_FLAG_HFP;
  set_hfp_callbacks(iodev);

  return iodev;
error:
  pcm_free_base_resources(hfpio);
  free(hfpio);
  return NULL;
}

void hfp_pcm_iodev_destroy(struct cras_iodev* iodev) {
  struct fl_pcm_io* hfpio = (struct fl_pcm_io*)iodev;

  byte_buffer_destroy(&hfpio->pcm_buf);
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    cras_iodev_list_rm_output(iodev);
  } else if (iodev->direction == CRAS_STREAM_INPUT) {
    cras_iodev_list_rm_input(iodev);
  }
  pcm_free_base_resources(hfpio);
  cras_iodev_free_resources(iodev);
  free(hfpio);
}

/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_sr_bt_adapters.h"

#include <syslog.h>
#include <time.h>

#include "cras/src/common/sample_buffer.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_sr.h"
#include "cras_util.h"

#define DEFAULT_SAMPLE_BUFFER_SIZE (28800)

struct cras_iodev_sr_bt_adapter {
  struct cras_iodev* iodev;
  struct cras_sr* sr;
  struct sample_buffer input_buf;
  struct sample_buffer output_buf;
  struct timespec prev_process_time;
};

struct cras_iodev_sr_bt_adapter* cras_iodev_sr_bt_adapter_create(
    struct cras_iodev* iodev,
    struct cras_sr* sr) {
  if (!iodev) {
    syslog(LOG_ERR,
           "cras_iodev_sr_bt_adapter_create failed due to NULL iodev.");
    return NULL;
  }

  if (!sr) {
    syslog(LOG_ERR, "cras_iodev_sr_bt_adapter_create failed due to NULL sr.");
    return NULL;
  }

  struct cras_iodev_sr_bt_adapter* adapter = NULL;
  adapter = calloc(1, sizeof(*adapter));
  if (!adapter) {
    syslog(LOG_ERR, "cras_iodev_sr_bt_adapter calloc failed.");
    return NULL;
  }

  if (sample_buffer_init(
          DEFAULT_SAMPLE_BUFFER_SIZE / cras_sr_get_frames_ratio(sr),
          sizeof(int16_t), &adapter->input_buf) != 0) {
    syslog(LOG_ERR,
           "cras_iodev_sr_bt_adapter_create failed due to input_buf init "
           "failure.");
    goto cras_iodev_sr_bt_adapter_create_failed;
  }

  if (sample_buffer_init(DEFAULT_SAMPLE_BUFFER_SIZE, sizeof(int16_t),
                         &adapter->output_buf) != 0) {
    syslog(LOG_ERR,
           "cras_iodev_sr_bt_adapter_create failed due to output_buf init "
           "failure.");
    goto cras_iodev_sr_bt_adapter_create_failed;
  }

  adapter->iodev = iodev;
  adapter->sr = sr;
  return adapter;

cras_iodev_sr_bt_adapter_create_failed:
  cras_iodev_sr_bt_adapter_destroy(adapter);
  return NULL;
}

void cras_iodev_sr_bt_adapter_destroy(
    struct cras_iodev_sr_bt_adapter* adapter) {
  if (!adapter) {
    return;
  }
  sample_buffer_cleanup(&adapter->input_buf);
  sample_buffer_cleanup(&adapter->output_buf);
  free(adapter);
}

/* Copies the data in the mono area into the sample buffer.
 * Args:
 *    area - The data area to be copied from.
 *    buf - The sample buffer to be copied to.
 * Returns:
 *    The number of frames read from the area.
 */
static unsigned copy_mono_area_to_sample_buffer(
    const struct cras_audio_area* area,
    struct sample_buffer* buf) {
  assert(area->num_channels == 1);
  assert(area->channels[0].step_bytes == sizeof(int16_t));

  const unsigned ori_src_frames = area->frames;
  unsigned remaining_frames = ori_src_frames;
  for (int i = 0; i < 2 && remaining_frames; ++i) {
    const unsigned writable_frames = sample_buf_writable(buf);
    if (writable_frames == 0) {
      break;
    }
    const unsigned written_frames = MIN(remaining_frames, writable_frames);
    memcpy(sample_buf_write_pointer(buf), area->channels[0].buf,
           written_frames * sample_buf_get_sample_size(buf));
    sample_buf_increment_write(buf, written_frames);
    remaining_frames -= written_frames;
  }
  return ori_src_frames - remaining_frames;
}

static int cras_iodev_sr_bt_adapter_propagate(
    struct cras_iodev_sr_bt_adapter* adapter,
    unsigned frames) {
  int rc = 0;

  struct cras_iodev* iodev = adapter->iodev;
  struct cras_audio_area* area = NULL;

  rc = iodev->get_buffer(iodev, &area, &frames);
  if (rc != 0) {
    syslog(LOG_WARNING, "iodev->get_buffer return non-zero code %d", rc);
    return rc;
  }

  const unsigned used_frames =
      copy_mono_area_to_sample_buffer(area, &adapter->input_buf);

  rc = iodev->put_buffer(iodev, used_frames);
  if (rc != 0) {
    syslog(LOG_WARNING, "iodev->put_buffer return non-zero code %d", rc);
    return rc;
  }

  cras_sr_process(adapter->sr, sample_buf_get_buf(&adapter->input_buf),
                  sample_buf_get_buf(&adapter->output_buf));

  return 0;
}

static inline bool can_invoke_model(struct cras_iodev_sr_bt_adapter* adapter,
                                    const struct timespec* tstamp) {
  if (sample_buf_queued(&adapter->output_buf) == 0) {
    return true;
  }

  // Limits the invocation frequencies by adding a time gap between adjacent
  // calls.
  struct timespec diff;
  subtract_timespecs(tstamp, &adapter->prev_process_time, &diff);
  return timespec_to_ms(&diff) >= 5;
}

int cras_iodev_sr_bt_adapter_frames_queued(
    struct cras_iodev_sr_bt_adapter* adapter,
    struct timespec* tstamp) {
  struct cras_iodev* iodev = adapter->iodev;
  int num_queued_sr_inputs = iodev->frames_queued(iodev, tstamp);

  if (can_invoke_model(adapter, tstamp)) {
    // Resets to decrease the probability of reaching end of the buffer.
    if (sample_buf_queued(&adapter->output_buf) == 0) {
      sample_buf_reset(&adapter->output_buf);
    }

    // Invokes the model with capped number of frames.
    cras_iodev_sr_bt_adapter_propagate(
        adapter,
        MIN(num_queued_sr_inputs, cras_sr_get_num_frames_per_run(adapter->sr) /
                                      cras_sr_get_frames_ratio(adapter->sr)));

    // Gets the remaining frames.
    num_queued_sr_inputs = iodev->frames_queued(iodev, tstamp);

    // Records the time.
    adapter->prev_process_time = *tstamp;
  }

  return cras_sr_get_frames_ratio(adapter->sr) * num_queued_sr_inputs +
         sample_buf_queued(&adapter->output_buf);
}

int cras_iodev_sr_bt_adapter_delay_frames(
    struct cras_iodev_sr_bt_adapter* adapter) {
  return adapter->iodev->delay_frames(adapter->iodev) *
         cras_sr_get_frames_ratio(adapter->sr);
}

int cras_iodev_sr_bt_adapter_get_buffer(
    struct cras_iodev_sr_bt_adapter* adapter,
    struct cras_audio_area** area,
    unsigned* frames) {
  const unsigned requested_frames = *frames;
  struct cras_iodev* iodev = adapter->iodev;

  unsigned readable_frames = 0;
  uint8_t* buf_ptr =
      sample_buf_read_pointer_size(&adapter->output_buf, &readable_frames);

  if (readable_frames > requested_frames) {
    readable_frames = requested_frames;
  }

  iodev->area->channels[0].buf = buf_ptr;
  iodev->area->frames = readable_frames;
  *area = iodev->area;
  *frames = iodev->area->frames;

  return 0;
}

int cras_iodev_sr_bt_adapter_put_buffer(
    struct cras_iodev_sr_bt_adapter* adapter,
    const unsigned nread) {
  const unsigned readable = sample_buf_readable(&adapter->output_buf);
  if (nread > readable) {
    syslog(LOG_WARNING, "put_buffer nread (%u) must be <= readable (%u).",
           nread, readable);
    return -EINVAL;
  }

  sample_buf_increment_read(&adapter->output_buf, nread);
  return 0;
}

// Flushes and resets the sample buf, returns the number of samples flushed.
static inline unsigned flush_sample_buffer(struct sample_buffer* buf) {
  unsigned flushed = 0;
  for (int i = 0; i < 2 && sample_buf_readable(buf); ++i) {
    const unsigned read = sample_buf_readable(buf);
    flushed += read;
    sample_buf_increment_read(buf, read);
  }
  sample_buf_reset(buf);
  return flushed;
}

int cras_iodev_sr_bt_adapter_flush_buffer(
    struct cras_iodev_sr_bt_adapter* adapter) {
  int flushed = adapter->iodev->flush_buffer(adapter->iodev);
  flushed += flush_sample_buffer(&adapter->input_buf);
  flushed *= cras_sr_get_frames_ratio(adapter->sr);
  flushed += flush_sample_buffer(&adapter->output_buf);
  return (int)flushed;
}

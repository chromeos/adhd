/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_floop_iodev.h"

#include <stdio.h>
#include <sys/syslog.h>
#include <time.h>

#include "cras/src/common/byte_buffer.h"
#include "cras/src/server/audio_thread_log.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_rstream_config.h"
#include "cras_audio_format.h"
#include "cras_types.h"
#include "third_party/superfasthash/sfh.h"

/*
 * Flexible loopback stream lifecycle:
 * +--------------+---------------+----------------------+
 * |              | no capture    | has capture          |
 * |              | streams       | streams              |
 * +--------------+---------------+----------------------+
 * | no playback  | A: do nothing | C: feed zero samples |
 * | streams      |               | to capture streams   |
 * +--------------+---------------+----------------------+
 * | has playback | B: do nothing | D: playback streams  |
 * | streams      |               | are attached to the  |
 * |              |               | output iodev         |
 * +--------------+---------------+----------------------+
 *
 * [B->D]
 * input_configure_dev calls cras_iodev_list_enable_floop_pair,
 * where streams matched with cras_floop_pair_match_output_stream
 * are added to the floop odev.
 *
 * [D->B]
 * input_close_dev calls cras_iodev_list_disable_floop_pair,
 * where all streams are removed from the floop odev.
 *
 * [C->D]
 * stream_added_cb calls cras_floop_pair_match_output_stream
 * to check for floop odevs that the stream should be attached to.
 */

#define LOOPBACK_BUFFER_SIZE 8192

static size_t loopback_supported_rates[] = {48000, 0};

static size_t loopback_supported_channel_counts[] = {2, 0};

static snd_pcm_format_t loopback_supported_formats[] = {
    SND_PCM_FORMAT_S16_LE,
    (snd_pcm_format_t)0,
};

struct flexible_loopback {
  struct cras_floop_pair pair;
  struct cras_floop_params params;
  struct timespec dev_start_time;
  struct byte_buffer* buffer;
  bool input_active;
  unsigned int read_frames;
};

static const struct flexible_loopback* const_pair_to_floop(
    const struct cras_floop_pair* pair) {
  return (struct flexible_loopback*)pair;
}

static struct flexible_loopback* input_to_floop(struct cras_iodev* iodev) {
  return (struct flexible_loopback*)iodev;
}

static const struct flexible_loopback* const_input_to_floop(
    const struct cras_iodev* iodev) {
  return (const struct flexible_loopback*)iodev;
}

static struct flexible_loopback* output_to_floop(struct cras_iodev* iodev) {
  return (struct flexible_loopback*)((char*)iodev -
                                     offsetof(struct cras_floop_pair, output));
}

static const struct flexible_loopback* const_output_to_floop(
    const struct cras_iodev* iodev) {
  return (const struct flexible_loopback*)((char*)iodev -
                                           offsetof(struct cras_floop_pair,
                                                    output));
}

/*
 * iodev callbacks.
 */

static int input_frames_queued(const struct cras_iodev* iodev,
                               struct timespec* tstamp) {
  clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
  const struct flexible_loopback* floop = const_input_to_floop(iodev);
  unsigned int frame_bytes = cras_get_format_bytes(iodev->format);

  /*
   *	When there is no output stream, fill the buffer with frames until it
   *	reaches the number of frames that is expected according to frame rate.
   */
  if (floop->input_active && !floop->pair.output.streams) {
    unsigned int frames_since_start = cras_frames_since_time(
        &floop->dev_start_time, iodev->format->frame_rate);
    unsigned int frames_to_fill = frames_since_start > floop->read_frames
                                      ? frames_since_start - floop->read_frames
                                      : 0;
    frames_to_fill =
        MIN(buf_writable(floop->buffer) / frame_bytes, frames_to_fill);
    if (frames_to_fill > 0) {
      unsigned int bytes_to_fill = frames_to_fill * frame_bytes;
      memset(buf_write_pointer(floop->buffer), 0, bytes_to_fill);
      buf_increment_write(floop->buffer, bytes_to_fill);
    }
  }

  return buf_queued(floop->buffer) / frame_bytes;
}

static int input_delay_frames(const struct cras_iodev* iodev) {
  return 0;
}

static int input_close_dev(struct cras_iodev* iodev) {
  struct flexible_loopback* floop = input_to_floop(iodev);
  floop->input_active = false;
  cras_iodev_list_disable_floop_pair(&floop->pair);
  cras_iodev_free_audio_area(iodev);
  buf_reset(floop->buffer);
  return 0;
}

static int input_configure_dev(struct cras_iodev* iodev) {
  struct flexible_loopback* floop = input_to_floop(iodev);

  // Must set active first.
  // Otherwise cras_floop_pair_match_output_stream always returns false.
  floop->input_active = true;

  cras_iodev_init_audio_area(iodev, iodev->format->num_channels);
  cras_iodev_list_enable_floop_pair(&floop->pair);
  clock_gettime(CLOCK_MONOTONIC_RAW, &floop->dev_start_time);
  return 0;
}

static int input_get_buffer(struct cras_iodev* iodev,
                            struct cras_audio_area** area,
                            unsigned* frames) {
  struct flexible_loopback* floop = input_to_floop(iodev);
  struct byte_buffer* buf = floop->buffer;
  unsigned int frame_bytes = cras_get_format_bytes(iodev->format);
  unsigned int avail_frames = buf_readable(buf) / frame_bytes;

  *frames = MIN(avail_frames, *frames);
  iodev->area->frames = *frames;
  cras_audio_area_config_buf_pointers(iodev->area, iodev->format,
                                      buf_read_pointer(buf));
  *area = iodev->area;

  return 0;
}

static int input_put_buffer(struct cras_iodev* iodev, unsigned nframes) {
  struct flexible_loopback* floop = input_to_floop(iodev);
  struct byte_buffer* buf = floop->buffer;
  unsigned int frame_bytes = cras_get_format_bytes(iodev->format);

  floop->read_frames += nframes;
  buf_increment_read(buf, (size_t)nframes * (size_t)frame_bytes);
  return 0;
}

static int input_flush_buffer(struct cras_iodev* iodev) {
  struct flexible_loopback* floop = input_to_floop(iodev);
  struct byte_buffer* buf = floop->buffer;
  unsigned int queued_bytes = buf_queued(buf);
  buf_reset(buf);
  return queued_bytes / cras_get_format_bytes(iodev->format);
}

/*
 * output iodev callbacks
 */

static int output_frames_queued(const struct cras_iodev* iodev,
                                struct timespec* tstamp) {
  clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
  const struct flexible_loopback* floop = const_output_to_floop(iodev);
  unsigned int frame_bytes = cras_get_format_bytes(iodev->format);
  return buf_queued(floop->buffer) / frame_bytes;
}

static int output_delay_frames(const struct cras_iodev* iodev) {
  return 0;
}

static int output_close_dev(struct cras_iodev* iodev) {
  return 0;
}

static int output_configure_dev(struct cras_iodev* iodev) {
  cras_iodev_init_audio_area(iodev, iodev->format->num_channels);
  return 0;
}

static int output_get_buffer(struct cras_iodev* iodev,
                             struct cras_audio_area** area,
                             unsigned* frames) {
  struct flexible_loopback* floop = output_to_floop(iodev);
  struct byte_buffer* buf = floop->buffer;
  unsigned int frame_bytes = cras_get_format_bytes(iodev->format);
  unsigned int avail_frames = buf_writable(buf) / frame_bytes;

  *frames = MIN(avail_frames, *frames);
  iodev->area->frames = *frames;
  cras_audio_area_config_buf_pointers(iodev->area, iodev->format,
                                      buf_write_pointer(buf));
  *area = iodev->area;

  return 0;
}

static int output_put_buffer(struct cras_iodev* iodev, unsigned nframes) {
  struct flexible_loopback* floop = output_to_floop(iodev);
  struct byte_buffer* buf = floop->buffer;
  size_t frame_bytes = cras_get_format_bytes(iodev->format);

  buf_increment_write(buf, (size_t)nframes * frame_bytes);
  return 0;
}

static int output_flush_buffer(struct cras_iodev* iodev) {
  return 0;
}

static void common_update_active_node(struct cras_iodev* iodev,
                                      unsigned node_idx,
                                      unsigned dev_enabled) {}

/*
 * Loopback devices are forced to be stereo. However, the channel
 * layout is not created to match the force assigment. This
 * function should set the channel layout as default, that is
 * FL, FR in this case.
 */
static int floop_update_channel_layout(struct cras_iodev* iodev) {
  cras_audio_format_set_default_channel_layout(iodev->format);

  return 0;
}

/*
 * iodev creation
 */

static void common_init_iodev(const struct cras_floop_params* params,
                              struct cras_iodev* iodev,
                              const char* name,
                              enum CRAS_NODE_TYPE node_type) {
  iodev->supported_rates = loopback_supported_rates;
  iodev->supported_channel_counts = loopback_supported_channel_counts;
  iodev->supported_formats = loopback_supported_formats;
  iodev->buffer_size = LOOPBACK_BUFFER_SIZE;
  iodev->update_active_node = common_update_active_node;
  iodev->update_channel_layout = floop_update_channel_layout;

  int namelen =
      snprintf(iodev->info.name, sizeof(iodev->info.name), "%s", name);
  namelen = MIN(namelen, sizeof(iodev->info.name) - 1);

  uint32_t hash = SuperFastHash(iodev->info.name, namelen, namelen);
  iodev->info.stable_id =
      SuperFastHash((const char*)params, sizeof(*params), hash);

  iodev->info.max_supported_channels = loopback_supported_channel_counts[0];

  struct cras_ionode* node = (struct cras_ionode*)calloc(1, sizeof(*node));
  node->dev = iodev;
  node->type = node_type;
  node->plugged = 1;
  node->volume = 100;
  node->ui_gain_scaler = 1;
  node->stable_id = iodev->info.stable_id;
  node->software_volume_needed = 0;
  snprintf(node->name, sizeof(node->name), "%s", name);
  cras_iodev_add_node(iodev, node);
  cras_iodev_set_active_node(iodev, node);
}

struct cras_floop_pair* cras_floop_pair_create(
    const struct cras_floop_params* params) {
  struct flexible_loopback* floop =
      (struct flexible_loopback*)calloc(1, sizeof(*floop));
  if (floop == NULL) {
    return NULL;
  }

  memcpy(&floop->params, params, sizeof(floop->params));

  floop->buffer = byte_buffer_create(LOOPBACK_BUFFER_SIZE * 4);
  if (floop->buffer == NULL) {
    free(floop);
    return NULL;
  }

  struct cras_iodev* input = &floop->pair.input;
  input->direction = CRAS_STREAM_INPUT;
  input->frames_queued = input_frames_queued;
  input->delay_frames = input_delay_frames;
  input->configure_dev = input_configure_dev;
  input->close_dev = input_close_dev;
  input->get_buffer = input_get_buffer;
  input->put_buffer = input_put_buffer;
  input->flush_buffer = input_flush_buffer;
  common_init_iodev(params, input, "Flexible Loopback", CRAS_NODE_TYPE_FLOOP);

  struct cras_iodev* output = &floop->pair.output;
  output->direction = CRAS_STREAM_OUTPUT;
  output->frames_queued = output_frames_queued;
  output->delay_frames = output_delay_frames;
  output->configure_dev = output_configure_dev;
  output->close_dev = output_close_dev;
  output->get_buffer = output_get_buffer;
  output->put_buffer = output_put_buffer;
  output->flush_buffer = output_flush_buffer;
  output->no_stream = cras_iodev_default_no_stream_playback;
  common_init_iodev(params, output, "Flexible Loopback (internal)",
                    CRAS_NODE_TYPE_FLOOP_INTERNAL);

  cras_iodev_list_add_input(&floop->pair.input);
  cras_iodev_list_add_output(&floop->pair.output);

  return &floop->pair;
}

void cras_floop_pair_destroy(struct cras_floop_pair* fpair) {
  if (!fpair) {
    return;
  }
  struct flexible_loopback* floop = (struct flexible_loopback*)fpair;

  cras_iodev_list_rm_input(&fpair->input);
  cras_iodev_list_rm_output(&fpair->output);
  free(fpair->input.nodes);
  free(fpair->output.nodes);

  byte_buffer_destroy(&floop->buffer);

  free(fpair);
}

bool cras_floop_pair_match_output_stream(const struct cras_floop_pair* pair,
                                         const struct cras_rstream* stream) {
  const struct flexible_loopback* floop = const_pair_to_floop(pair);
  return stream->direction == CRAS_STREAM_OUTPUT && floop->input_active &&
         (floop->params.client_types_mask & (1 << stream->client_type));
}

bool cras_floop_pair_match_params(const struct cras_floop_pair* pair,
                                  const struct cras_floop_params* params) {
  const struct flexible_loopback* floop = const_pair_to_floop(pair);
  return floop->params.client_types_mask == params->client_types_mask;
}

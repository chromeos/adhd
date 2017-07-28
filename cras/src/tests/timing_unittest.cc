// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include <gtest/gtest.h>

extern "C" {
#include "dev_io.h" // tested
#include "dev_stream.h" // tested
#include "cras_rstream.h" // stubbed
#include "cras_iodev.h" // stubbed
#include "cras_shm.h"
#include "cras_types.h"
#include "utlist.h"

struct audio_thread_event_log* atlog;
}

#include "iodev_stub.h"
#include "rstream_stub.h"

namespace {

using DevStreamPtr = std::unique_ptr<dev_stream, decltype(free)*>;
using IodevPtr = std::unique_ptr<cras_iodev, decltype(free)*>;
using IonodePtr = std::unique_ptr<cras_ionode, decltype(free)*>;
using OpendevPtr = std::unique_ptr<open_dev, decltype(free)*>;
using RstreamPtr = std::unique_ptr<cras_rstream, decltype(free)*>;
using ShmPtr = std::unique_ptr<cras_audio_shm_area, decltype(free)*>;

// Holds the rstream and devstream pointers for an attached stream.
struct Stream {
  Stream(ShmPtr shm, RstreamPtr rstream, DevStreamPtr dstream) :
    shm(std::move(shm)),
    rstream(std::move(rstream)),
    dstream(std::move(dstream)) {
  }
  ShmPtr shm;
  RstreamPtr rstream;
  DevStreamPtr dstream;
};
using StreamPtr = std::unique_ptr<Stream>;

// Holds the iodev and ionode pointers for an attached device.
struct Device {
  Device(IodevPtr dev, IonodePtr node, OpendevPtr odev) :
    dev(std::move(dev)),
    node(std::move(node)),
    odev(std::move(odev)) {
  }
  IodevPtr dev;
  IonodePtr node;
  OpendevPtr odev;
};
using DevicePtr = std::unique_ptr<Device>;

ShmPtr create_shm(size_t cb_threshold) {
  uint32_t frame_bytes = 4;
  uint32_t used_size = cb_threshold * 2 * frame_bytes;
  uint32_t shm_size = sizeof(cras_audio_shm_area) + used_size;
  ShmPtr shm(reinterpret_cast<cras_audio_shm_area*>(calloc(1, shm_size)),
              free);
  shm->config.used_size = used_size;
  shm->config.frame_bytes = frame_bytes;
  shm->volume_scaler = 1.0;
  return shm;
}

RstreamPtr create_rstream(cras_stream_id_t id,
                          CRAS_STREAM_DIRECTION direction,
                          size_t cb_threshold,
                          const cras_audio_format* format,
                          cras_audio_shm_area* shm) {
  RstreamPtr rstream(
      reinterpret_cast<cras_rstream*>(calloc(1, sizeof(cras_rstream))), free);
  rstream->stream_id = id;
  rstream->direction = direction;
  rstream->fd = -1;
  rstream->buffer_frames = cb_threshold * 2;
  rstream->cb_threshold = cb_threshold;
  rstream->shm.area = shm;
  rstream->shm.config = shm->config;
  rstream->format = *format;
  cras_frames_to_time(cb_threshold,
                      rstream->format.frame_rate,
                      &rstream->sleep_interval_ts);
  return rstream;
}

DevStreamPtr create_dev_stream(unsigned int dev_id, cras_rstream* rstream) {
  DevStreamPtr dstream(
      reinterpret_cast<dev_stream*>(calloc(1, sizeof(dev_stream))),
      free);
  dstream->dev_id = dev_id;
  dstream->stream = rstream;
  dstream->dev_rate = rstream->format.frame_rate;
  return dstream;
}

StreamPtr create_stream(cras_stream_id_t id,
                        unsigned int dev_id,
                        CRAS_STREAM_DIRECTION direction,
                        size_t cb_threshold,
                        const cras_audio_format* format) {
  ShmPtr shm = create_shm(cb_threshold);
  RstreamPtr rstream = create_rstream(1, CRAS_STREAM_INPUT, cb_threshold,
                                      format, shm.get());
  DevStreamPtr dstream = create_dev_stream(1, rstream.get());
  StreamPtr s(new Stream(std::move(shm),
                         std::move(rstream),
                         std::move(dstream)));
  return s;
}

int delay_frames_stub(const struct cras_iodev* iodev) {
  return 0;
}

IonodePtr create_ionode(CRAS_NODE_TYPE type) {
  IonodePtr ionode(
      reinterpret_cast<cras_ionode*>(calloc(1, sizeof(cras_ionode))), free);
  ionode->type = type;
  return ionode;
}

IodevPtr create_open_iodev(CRAS_STREAM_DIRECTION direction,
                           size_t cb_threshold,
                           cras_audio_format* format,
                           cras_ionode* active_node) {
  IodevPtr iodev(reinterpret_cast<cras_iodev*>(calloc(1, sizeof(cras_iodev))),
                  free);
  iodev->is_enabled = 1;
  iodev->direction = direction;
  iodev->format = format;
  iodev->state = CRAS_IODEV_STATE_OPEN;
  iodev->delay_frames = delay_frames_stub;
  iodev->active_node = active_node;
  iodev->buffer_size = cb_threshold * 2;
  iodev->min_cb_level = UINT_MAX;
  iodev->max_cb_level = 0;
  return iodev;
}

DevicePtr create_device(CRAS_STREAM_DIRECTION direction,
                        size_t cb_threshold,
                        cras_audio_format* format,
                        CRAS_NODE_TYPE active_node_type) {
  IonodePtr node = create_ionode(active_node_type);
  IodevPtr dev = create_open_iodev(direction, cb_threshold, format, node.get());
  OpendevPtr odev(
      reinterpret_cast<open_dev*>(calloc(1, sizeof(open_dev))), free);
  odev->dev = dev.get();

  DevicePtr d(new Device(std::move(dev), std::move(node), std::move(odev)));
  return d;
}

void add_stream_to_dev(IodevPtr& dev, const StreamPtr& stream) {
  DL_APPEND(dev->streams, stream->dstream.get());
  dev->min_cb_level = std::min(stream->rstream->cb_threshold,
                               static_cast<size_t>(dev->min_cb_level));
  dev->max_cb_level = std::max(stream->rstream->cb_threshold,
                               static_cast<size_t>(dev->max_cb_level));
}

void fill_audio_format(cras_audio_format* format, unsigned int rate) {
  format->format = SND_PCM_FORMAT_S16_LE;
  format->frame_rate = rate;
  format->num_channels = 2;
  format->channel_layout[0] = 0;
  format->channel_layout[1] = 1;
  for (int i = 2; i < CRAS_CH_MAX; i++)
    format->channel_layout[i] = -1;
}

class TimingSuite : public testing::Test{
 protected:
  virtual void SetUp() {
    atlog = static_cast<audio_thread_event_log*>(calloc(1, sizeof(*atlog)));
    iodev_stub_reset();
    rstream_stub_reset();
  }

  virtual void TearDown() {
    free(atlog);
  }

  timespec SingleInputDevNextWake(
      size_t dev_cb_threshold,
      size_t dev_level,
      const timespec* level_timestamp,
      cras_audio_format* dev_format,
      const std::vector<StreamPtr>& streams) {
    struct open_dev* dev_list_ = NULL;

    DevicePtr dev = create_device(CRAS_STREAM_INPUT, dev_cb_threshold,
                                  dev_format, CRAS_NODE_TYPE_MIC);
    DL_APPEND(dev_list_, dev->odev.get());

    for (auto const& stream : streams) {
      add_stream_to_dev(dev->dev, stream);
    }

    // Set response for frames_queued.
    iodev_stub_frames_queued(dev->dev.get(), dev_level, *level_timestamp);

    dev_io_send_captured_samples(dev_list_);

    struct timespec dev_time;
    dev_time.tv_sec = level_timestamp->tv_sec + 500; // Far in the future.
    dev_io_next_input_wake(&dev_list_, &dev_time);
    return dev_time;
  }
};

TEST_F(TimingSuite, WaitAfterFill) {
  const size_t cb_threshold = 480;

  cras_audio_format format;
  fill_audio_format(&format, 48000);

  StreamPtr stream =
      create_stream(1, 1, CRAS_STREAM_INPUT, cb_threshold, &format);
  // rstream's next callback is now and there is enough data to fill.
  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  stream->rstream->next_cb_ts = start;
  cras_shm_buffer_written(&stream->rstream->shm, 480);

  std::vector<StreamPtr> streams;
  streams.emplace_back(std::move(stream));
  timespec dev_time = SingleInputDevNextWake(cb_threshold, 0, &start,
                                             &format, streams);

  // The next callback should be scheduled 10ms in the future.
  // And the next wake up should reflect the only attached stream.
  EXPECT_EQ(dev_time.tv_sec, streams[0]->rstream->next_cb_ts.tv_sec);
  EXPECT_EQ(dev_time.tv_nsec, streams[0]->rstream->next_cb_ts.tv_nsec);
}

TEST_F(TimingSuite, WaitTwoStreamsSameFormat) {
  const size_t cb_threshold = 480;

  cras_audio_format format;
  fill_audio_format(&format, 48000);

  // stream1's next callback is now and there is enough data to fill.
  StreamPtr stream1 =
      create_stream(1, 1, CRAS_STREAM_INPUT, cb_threshold, &format);
  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  stream1->rstream->next_cb_ts = start;
  cras_shm_buffer_written(&stream1->rstream->shm, cb_threshold);
  // stream2 is only half full.
  StreamPtr stream2  =
      create_stream(1, 1, CRAS_STREAM_INPUT, cb_threshold, &format);
  stream2->rstream->next_cb_ts = start;
  cras_shm_buffer_written(&stream2->rstream->shm, 240);
  rstream_stub_dev_offset(stream2->rstream.get(), 1, 240);

  std::vector<StreamPtr> streams;
  streams.emplace_back(std::move(stream1));
  streams.emplace_back(std::move(stream2));
  timespec dev_time = SingleInputDevNextWake(cb_threshold, 0, &start,
                                             &format, streams);

  // Should wait for approximately 5 milliseconds for 240 samples at 48k.
  struct timespec delta2;
  subtract_timespecs(&dev_time, &start, &delta2);
  EXPECT_LT(4900 * 1000, delta2.tv_nsec);
  EXPECT_GT(5100 * 1000, delta2.tv_nsec);
}

/* Stubs */
extern "C" {

int cras_server_metrics_longest_fetch_delay(unsigned delay_msec) {
  return 0;
}

}  // extern "C"

}  //  namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

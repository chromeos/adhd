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

#define FAKE_POLL_FD 33

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
  uint32_t shm_size = sizeof(cras_audio_shm_area) + used_size * 2;
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
  rstream->fd = FAKE_POLL_FD;
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

void AddFakeDataToStream(Stream* stream, unsigned int frames) {
  cras_shm_check_write_overrun(&stream->rstream->shm);
  cras_shm_buffer_written(&stream->rstream->shm, frames);
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

// One device, one stream, write a callback of data and check the sleep time is
// one more wakeup interval.
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
  AddFakeDataToStream(stream.get(), 480);

  std::vector<StreamPtr> streams;
  streams.emplace_back(std::move(stream));
  timespec dev_time = SingleInputDevNextWake(cb_threshold, 0, &start,
                                             &format, streams);

  // The next callback should be scheduled 10ms in the future.
  // And the next wake up should reflect the only attached stream.
  EXPECT_EQ(dev_time.tv_sec, streams[0]->rstream->next_cb_ts.tv_sec);
  EXPECT_EQ(dev_time.tv_nsec, streams[0]->rstream->next_cb_ts.tv_nsec);
}

// One device(48k), one stream(44.1k), write a callback of data and check that
// the sleep time is correct when doing SRC.
TEST_F(TimingSuite, WaitAfterFillSRC) {
  cras_audio_format dev_format;
  fill_audio_format(&dev_format, 48000);
  cras_audio_format stream_format;
  fill_audio_format(&stream_format, 44100);

  StreamPtr stream =
      create_stream(1, 1, CRAS_STREAM_INPUT, 441, &stream_format);
  // rstream's next callback is now and there is enough data to fill.
  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  stream->rstream->next_cb_ts = start;
  AddFakeDataToStream(stream.get(), 441);

  std::vector<StreamPtr> streams;
  streams.emplace_back(std::move(stream));
  timespec dev_time = SingleInputDevNextWake(480, 0, &start,
                                             &dev_format, streams);

  // The next callback should be scheduled 10ms in the future.
  struct timespec delta;
  subtract_timespecs(&dev_time, &start, &delta);
  EXPECT_LT(9900 * 1000, delta.tv_nsec);
  EXPECT_GT(10100 * 1000, delta.tv_nsec);
}

// One device, two streams. One stream is ready the other still needs data.
// Checks that the sleep interval is based on the time the device will take to
// supply the needed samples for stream2.
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
  AddFakeDataToStream(stream1.get(), cb_threshold);

  // stream2 is only half full.
  StreamPtr stream2  =
      create_stream(1, 1, CRAS_STREAM_INPUT, cb_threshold, &format);
  stream2->rstream->next_cb_ts = start;
  AddFakeDataToStream(stream2.get(), 240);

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

// One device(44.1), two streams(44.1, 48). One stream is ready the other still
// needs data. Checks that the sleep interval is based on the time the device
// will take to supply the needed samples for stream2, stream2 is sample rate
// converted from the 44.1k device to the 48k stream.
TEST_F(TimingSuite, WaitTwoStreamsDifferentRates) {
  cras_audio_format s1_format, s2_format;
  fill_audio_format(&s1_format, 44100);
  fill_audio_format(&s2_format, 48000);

  // stream1's next callback is now and there is enough data to fill.
  StreamPtr stream1 =
      create_stream(1, 1, CRAS_STREAM_INPUT, 441, &s1_format);
  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  stream1->rstream->next_cb_ts = start;
  AddFakeDataToStream(stream1.get(), 441);
  // stream2's next callback is now but there is only half a callback of data.
  StreamPtr stream2  =
      create_stream(1, 1, CRAS_STREAM_INPUT, 480, &s2_format);
  stream2->rstream->next_cb_ts = start;
  AddFakeDataToStream(stream2.get(), 240);

  std::vector<StreamPtr> streams;
  streams.emplace_back(std::move(stream1));
  streams.emplace_back(std::move(stream2));
  timespec dev_time = SingleInputDevNextWake(441, 0, &start,
                                             &s1_format, streams);

  // Should wait for approximately 5 milliseconds for 240 48k samples from the
  // 44.1k device.
  struct timespec delta2;
  subtract_timespecs(&dev_time, &start, &delta2);
  EXPECT_LT(4900 * 1000, delta2.tv_nsec);
  EXPECT_GT(5100 * 1000, delta2.tv_nsec);
}

// One device, two streams. Both streams get a full callback of data and the
// device has enough samples for the next callback already. Checks that the
// shorter of the two streams times is used for the next sleep interval.
TEST_F(TimingSuite, WaitTwoStreamsDifferentWakeupTimes) {
  cras_audio_format s1_format, s2_format;
  fill_audio_format(&s1_format, 44100);
  fill_audio_format(&s2_format, 48000);

  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);

  // stream1's next callback is in 3ms.
  StreamPtr stream1 =
      create_stream(1, 1, CRAS_STREAM_INPUT, 441, &s1_format);
  stream1->rstream->next_cb_ts = start;
  const timespec three_millis = { 0, 3 * 1000 * 1000 };
  add_timespecs(&stream1->rstream->next_cb_ts, &three_millis);
  AddFakeDataToStream(stream1.get(), 441);
  // stream2 is also ready next cb in 5ms..
  StreamPtr stream2  =
      create_stream(1, 1, CRAS_STREAM_INPUT, 480, &s2_format);
  stream2->rstream->next_cb_ts = start;
  const timespec five_millis = { 0, 5 * 1000 * 1000 };
  add_timespecs(&stream2->rstream->next_cb_ts, &five_millis);
  AddFakeDataToStream(stream1.get(), 480);

  std::vector<StreamPtr> streams;
  streams.emplace_back(std::move(stream1));
  streams.emplace_back(std::move(stream2));
  timespec dev_time = SingleInputDevNextWake(441, 441, &start,
                                             &s1_format, streams);

  // Should wait for approximately 3 milliseconds for stream 1 first.
  struct timespec delta2;
  subtract_timespecs(&dev_time, &start, &delta2);
  EXPECT_LT(2900 * 1000, delta2.tv_nsec);
  EXPECT_GT(3100 * 1000, delta2.tv_nsec);
}

// One hotword stream attaches to hotword device. Input data has copied from
// device to stream but total number is less than cb_threshold. Hotword stream
// should be scheduled wake base on the samples needed to fill full shm.
TEST_F(TimingSuite, HotwordStreamUseDevTiming) {
  cras_audio_format fmt;
  fill_audio_format(&fmt, 48000);

  struct timespec start, delay;
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);

  StreamPtr stream =
      create_stream(1, 1, CRAS_STREAM_INPUT, 240, &fmt);
  stream->rstream->flags = HOTWORD_STREAM;
  stream->rstream->next_cb_ts = start;
  delay.tv_sec = 0;
  delay.tv_nsec = 3 * 1000 * 1000;
  add_timespecs(&stream->rstream->next_cb_ts, &delay);

  // Add fake data to stream and device so its slightly less than cb_threshold.
  // Expect to wait for samples to fill the full buffer (480 - 192) frames
  // instead of using the next_cb_ts.
  AddFakeDataToStream(stream.get(), 192);
  std::vector<StreamPtr> streams;
  streams.emplace_back(std::move(stream));
  timespec dev_time = SingleInputDevNextWake(4096, 0, &start,
                                             &fmt, streams);
  struct timespec delta;
  subtract_timespecs(&dev_time, &start, &delta);
  // 288 frames worth of time = 6 ms.
  EXPECT_EQ(6 * 1000 * 1000, delta.tv_nsec);
}

// One hotword stream attaches to hotword device. Input data burst to a number
// larger than cb_threshold. In this case stream fd is used to poll for next
// wake. And the dev wake time is unchanged from the default 20 seconds limit.
TEST_F(TimingSuite, HotwordStreamBulkData) {
  cras_audio_format fmt;
  fill_audio_format(&fmt, 48000);

  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);

  StreamPtr stream =
      create_stream(1, 1, CRAS_STREAM_INPUT, 240, &fmt);
  stream->rstream->flags = HOTWORD_STREAM;
  stream->rstream->next_cb_ts = start;

  AddFakeDataToStream(stream.get(), 480);
  std::vector<StreamPtr> streams;
  streams.emplace_back(std::move(stream));
  timespec dev_time = SingleInputDevNextWake(4096, 7000, &start,
                                             &fmt, streams);

  int poll_fd = dev_stream_poll_stream_fd(streams[0]->dstream.get());
  EXPECT_EQ(FAKE_POLL_FD, poll_fd);

  struct timespec delta;
  subtract_timespecs(&dev_time, &start, &delta);
  EXPECT_LT(19, delta.tv_sec);
  EXPECT_GT(21, delta.tv_sec);
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

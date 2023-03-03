// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>

extern "C" {
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras_messages.h"
#include "cras_shm.h"
#include "cras_util.h"
}

#include "cras/src/tests/metrics_stub.h"

namespace {
static int buffer_share_get_new_write_point_ret;
static const struct cras_iodev* cras_stream_apm_get_active_idev;
static struct cras_stream_apm* cras_stream_apm_get_active_stream;
static struct cras_stream_apm* fake_stream_apm =
    reinterpret_cast<struct cras_stream_apm*>(0x123);

class RstreamTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    int rc;
    int sock[2] = {-1, -1};

    fmt_.format = SND_PCM_FORMAT_S16_LE;
    fmt_.frame_rate = 48000;
    fmt_.num_channels = 2;

    config_.stream_id = 555;
    config_.stream_type = CRAS_STREAM_TYPE_DEFAULT;
    config_.client_type = CRAS_CLIENT_TYPE_UNKNOWN;
    config_.direction = CRAS_STREAM_OUTPUT;
    config_.dev_idx = NO_DEVICE;
    config_.flags = 0;
    config_.format = &fmt_;
    config_.buffer_frames = 4096;
    config_.cb_threshold = 2048;
    config_.client_shm_size = 0;
    config_.client_shm_fd = -1;

    // Create a socket pair because it will be used in rstream.
    rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sock);
    ASSERT_EQ(0, rc);
    config_.audio_fd = sock[1];
    client_fd_ = sock[0];

    config_.client = NULL;
    buffer_share_get_new_write_point_ret = 0;
  }

  virtual void TearDown() {
    close(config_.audio_fd);
    close(client_fd_);
  }

  static bool format_equal(cras_audio_format* fmt1, cras_audio_format* fmt2) {
    return fmt1->format == fmt2->format &&
           fmt1->frame_rate == fmt2->frame_rate &&
           fmt1->num_channels == fmt2->num_channels;
  }

  void stub_client_reply(enum CRAS_AUDIO_MESSAGE_ID id, int frames, int err) {
    int rc;
    struct audio_message aud_msg;
    // Create a message.
    aud_msg.id = id;
    aud_msg.frames = frames;
    aud_msg.error = err;

    // Use socket fd to stub message from client.
    rc = write(client_fd_, &aud_msg, sizeof(aud_msg));
    EXPECT_EQ(sizeof(aud_msg), rc);
    return;
  }

  struct cras_audio_format fmt_;
  struct cras_rstream_config config_;
  int client_fd_;
};

TEST_F(RstreamTestSuite, InvalidDirection) {
  struct cras_rstream* s;
  int rc;

  config_.direction = (enum CRAS_STREAM_DIRECTION)66;
  rc = cras_rstream_create(&config_, &s);
  EXPECT_NE(0, rc);
}

TEST_F(RstreamTestSuite, InvalidStreamType) {
  struct cras_rstream* s;
  int rc;

  config_.stream_type = (enum CRAS_STREAM_TYPE)7;
  rc = cras_rstream_create(&config_, &s);
  EXPECT_NE(0, rc);
}

TEST_F(RstreamTestSuite, InvalidBufferSize) {
  struct cras_rstream* s;
  int rc;

  config_.buffer_frames = 3;
  rc = cras_rstream_create(&config_, &s);
  EXPECT_NE(0, rc);
}

TEST_F(RstreamTestSuite, InvalidCallbackThreshold) {
  struct cras_rstream* s;
  int rc;

  config_.cb_threshold = 3;
  rc = cras_rstream_create(&config_, &s);
  EXPECT_NE(0, rc);
}

TEST_F(RstreamTestSuite, InvalidStreamPointer) {
  int rc;

  rc = cras_rstream_create(&config_, NULL);
  EXPECT_NE(0, rc);
}

TEST_F(RstreamTestSuite, CreateOutput) {
  struct cras_rstream* s;
  struct cras_audio_format fmt_ret;
  struct cras_audio_shm* shm_ret;
  struct cras_audio_shm shm_mapped;
  int rc, header_fd = -1, samples_fd = -1;
  size_t shm_size;

  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);
  EXPECT_NE((void*)NULL, s);
  EXPECT_EQ(4096, cras_rstream_get_buffer_frames(s));
  EXPECT_EQ(2048, cras_rstream_get_cb_threshold(s));
  EXPECT_EQ(CRAS_STREAM_TYPE_DEFAULT, cras_rstream_get_type(s));
  EXPECT_EQ(CRAS_STREAM_OUTPUT, cras_rstream_get_direction(s));
  EXPECT_NE((void*)NULL, cras_rstream_shm(s));
  rc = cras_rstream_get_format(s, &fmt_ret);
  EXPECT_EQ(0, rc);
  EXPECT_TRUE(format_equal(&fmt_ret, &fmt_));

  // Check if shm is really set up.
  shm_ret = cras_rstream_shm(s);
  ASSERT_NE((void*)NULL, shm_ret);
  cras_rstream_get_shm_fds(s, &header_fd, &samples_fd);
  shm_size = cras_shm_samples_size(shm_ret);
  EXPECT_EQ(shm_size, 32768);
  shm_mapped.header = (struct cras_audio_shm_header*)mmap(
      NULL, cras_shm_header_size(), PROT_READ | PROT_WRITE, MAP_SHARED,
      header_fd, 0);
  EXPECT_NE((void*)NULL, shm_mapped.header);
  cras_shm_copy_shared_config(&shm_mapped);
  EXPECT_EQ(cras_shm_used_size(&shm_mapped), cras_shm_used_size(shm_ret));
  munmap(shm_mapped.header, cras_shm_header_size());

  cras_rstream_destroy(s);
}

TEST_F(RstreamTestSuite, TestNumDelayedFetch) {
  int rc;
  struct cras_rstream* s;

  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);
  EXPECT_NE((void*)NULL, s);
  EXPECT_EQ(4096, cras_rstream_get_buffer_frames(s));
  EXPECT_EQ(2048, cras_rstream_get_cb_threshold(s));
  EXPECT_EQ(CRAS_STREAM_TYPE_DEFAULT, cras_rstream_get_type(s));
  EXPECT_EQ(CRAS_STREAM_OUTPUT, cras_rstream_get_direction(s));

  struct timespec t0;
  struct timespec t1;
  struct timespec fetch_time;
  cras_frames_to_time(2048, 48000, &fetch_time);
  clock_gettime(CLOCK_MONOTONIC_RAW, &t0);
  t1 = t0;
  s->last_fetch_ts = t0;
  add_timespecs(&t1, &fetch_time);
  cras_rstream_record_fetch_interval(s, &t1);
  EXPECT_EQ(0, s->num_delayed_fetches);

  s->last_fetch_ts = t0;
  add_timespecs(&t1, &fetch_time);
  cras_rstream_record_fetch_interval(s, &t1);
  EXPECT_EQ(1, s->num_delayed_fetches);

  cras_rstream_destroy(s);
}

TEST_F(RstreamTestSuite, CreateInput) {
  struct cras_rstream* s;
  struct cras_audio_format fmt_ret;
  struct cras_audio_shm* shm_ret;
  struct cras_audio_shm shm_mapped;
  int rc, header_fd = -1, samples_fd = -1;
  size_t shm_size;

  config_.direction = CRAS_STREAM_INPUT;
  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);
  EXPECT_NE((void*)NULL, s);
  EXPECT_EQ(4096, cras_rstream_get_buffer_frames(s));
  EXPECT_EQ(2048, cras_rstream_get_cb_threshold(s));
  EXPECT_EQ(CRAS_STREAM_TYPE_DEFAULT, cras_rstream_get_type(s));
  EXPECT_EQ(CRAS_STREAM_INPUT, cras_rstream_get_direction(s));
  EXPECT_NE((void*)NULL, cras_rstream_shm(s));
  rc = cras_rstream_get_format(s, &fmt_ret);
  EXPECT_EQ(0, rc);
  EXPECT_TRUE(format_equal(&fmt_ret, &fmt_));

  // Check if shm is really set up.
  shm_ret = cras_rstream_shm(s);
  ASSERT_NE((void*)NULL, shm_ret);
  cras_rstream_get_shm_fds(s, &header_fd, &samples_fd);
  shm_size = cras_shm_samples_size(shm_ret);
  EXPECT_EQ(shm_size, 32768);
  shm_mapped.header = (struct cras_audio_shm_header*)mmap(
      NULL, cras_shm_header_size(), PROT_READ | PROT_WRITE, MAP_SHARED,
      header_fd, 0);
  EXPECT_NE((void*)NULL, shm_mapped.header);
  cras_shm_copy_shared_config(&shm_mapped);
  EXPECT_EQ(cras_shm_used_size(&shm_mapped), cras_shm_used_size(shm_ret));
  munmap(shm_mapped.header, cras_shm_header_size());

  cras_rstream_destroy(s);
}

TEST_F(RstreamTestSuite, VerifyStreamTypes) {
  struct cras_rstream* s;
  int rc;

  config_.stream_type = CRAS_STREAM_TYPE_DEFAULT;
  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(CRAS_STREAM_TYPE_DEFAULT, cras_rstream_get_type(s));
  EXPECT_NE(CRAS_STREAM_TYPE_MULTIMEDIA, cras_rstream_get_type(s));
  cras_rstream_destroy(s);

  config_.stream_type = CRAS_STREAM_TYPE_VOICE_COMMUNICATION;
  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(CRAS_STREAM_TYPE_VOICE_COMMUNICATION, cras_rstream_get_type(s));
  cras_rstream_destroy(s);

  config_.direction = CRAS_STREAM_INPUT;
  config_.stream_type = CRAS_STREAM_TYPE_SPEECH_RECOGNITION;
  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(CRAS_STREAM_TYPE_SPEECH_RECOGNITION, cras_rstream_get_type(s));
  cras_rstream_destroy(s);

  config_.stream_type = CRAS_STREAM_TYPE_PRO_AUDIO;
  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(CRAS_STREAM_TYPE_PRO_AUDIO, cras_rstream_get_type(s));
  cras_rstream_destroy(s);
}

TEST_F(RstreamTestSuite, OutputStreamIsPendingReply) {
  struct cras_rstream* s;
  int rc;
  struct timespec ts;

  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);

  // Not pending reply.
  rc = cras_rstream_is_pending_reply(s);
  EXPECT_EQ(0, rc);

  // Request some data from client.
  rc = cras_rstream_request_audio(s, &ts);
  EXPECT_GT(rc, 0);

  // Pending reply.
  rc = cras_rstream_is_pending_reply(s);
  EXPECT_EQ(1, rc);

  cras_rstream_destroy(s);
}

TEST_F(RstreamTestSuite, OutputStreamFlushMessages) {
  struct cras_rstream* s;
  int rc;
  struct timespec ts;

  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);

  // Not pending reply.
  rc = cras_rstream_is_pending_reply(s);
  EXPECT_EQ(0, rc);

  // Request some data from client.
  rc = cras_rstream_request_audio(s, &ts);
  EXPECT_GT(rc, 0);

  // Pending reply.
  rc = cras_rstream_is_pending_reply(s);
  EXPECT_EQ(1, rc);

  // Client replies that data is ready.
  stub_client_reply(AUDIO_MESSAGE_DATA_READY, 10, 0);

  // Read messages.
  cras_rstream_flush_old_audio_messages(s);

  // NOT Pending reply.
  rc = cras_rstream_is_pending_reply(s);
  EXPECT_EQ(0, rc);

  cras_rstream_destroy(s);
}

TEST_F(RstreamTestSuite, InputStreamIsPendingReply) {
  struct cras_rstream* s;
  int rc;

  config_.direction = CRAS_STREAM_INPUT;

  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);

  // Not pending reply.
  rc = cras_rstream_is_pending_reply(s);
  EXPECT_EQ(0, rc);

  // Some data is ready. Sends it to client.
  rc = cras_rstream_audio_ready(s, 10);
  EXPECT_GT(rc, 0);

  // Pending reply.
  rc = cras_rstream_is_pending_reply(s);
  EXPECT_EQ(1, rc);

  cras_rstream_destroy(s);
}

TEST_F(RstreamTestSuite, InputStreamFlushMessages) {
  struct cras_rstream* s;
  int rc;

  config_.direction = CRAS_STREAM_INPUT;

  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);

  // Not pending reply.
  rc = cras_rstream_is_pending_reply(s);
  EXPECT_EQ(0, rc);

  // Some data is ready. Sends it to client.
  rc = cras_rstream_audio_ready(s, 10);
  EXPECT_GT(rc, 0);

  // Pending reply.
  rc = cras_rstream_is_pending_reply(s);
  EXPECT_EQ(1, rc);

  // Client replies that data is captured.
  stub_client_reply(AUDIO_MESSAGE_DATA_CAPTURED, 10, 0);

  // Read messages.
  cras_rstream_flush_old_audio_messages(s);

  // NOT Pending reply.
  rc = cras_rstream_is_pending_reply(s);
  EXPECT_EQ(0, rc);

  cras_rstream_destroy(s);
}

TEST_F(RstreamTestSuite, UpdateOutputReadPtr) {
  struct cras_rstream* s;
  uint8_t* buf;
  int rc;
  int tmp = 1234;

  config_.direction = CRAS_STREAM_INPUT;
  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);

  // Test the scenario when data sits across double buffer in shm.
  buf = cras_shm_get_write_buffer_base(s->shm);
  cras_shm_buffer_written_start(s->shm, config_.cb_threshold);
  buf = cras_shm_get_write_buffer_base(s->shm);
  cras_shm_buffer_written_start(s->shm, tmp);

  // Device buffer share object says this amount can be marked as read.
  buffer_share_get_new_write_point_ret = config_.cb_threshold + tmp;
  cras_rstream_update_output_read_pointer(s);

  // Data sits across double buffer.
  buf = cras_shm_get_write_buffer_base(s->shm);
  cras_shm_buffer_written_start(s->shm, config_.cb_threshold - tmp);
  buf = cras_shm_get_write_buffer_base(s->shm);
  cras_shm_buffer_written_start(s->shm, tmp);

  buffer_share_get_new_write_point_ret = config_.cb_threshold;
  cras_rstream_update_output_read_pointer(s);

  (void)buf;
  cras_rstream_destroy(s);
}

TEST_F(RstreamTestSuite, EffectPostProcessingFormat) {
  int rc;
  struct cras_rstream* s;
  struct cras_iodev* fake_idev = reinterpret_cast<struct cras_iodev*>(0x456);

  config_.effects = APM_ECHO_CANCELLATION;
  config_.direction = CRAS_STREAM_INPUT;

  rc = cras_rstream_create(&config_, &s);
  EXPECT_EQ(0, rc);

  cras_stream_apm_get_active_stream = NULL;
  cras_rstream_post_processing_format(s, fake_idev);
  EXPECT_EQ(cras_stream_apm_get_active_stream, fake_stream_apm);
  EXPECT_EQ(cras_stream_apm_get_active_idev, fake_idev);

  cras_rstream_destroy(s);
}

}  //  namespace

// stubs
extern "C" {

struct cras_audio_area* cras_audio_area_create(int num_channels) {
  return NULL;
}

void cras_audio_area_destroy(struct cras_audio_area* area) {}

void cras_audio_area_config_channels(struct cras_audio_area* area,
                                     const struct cras_audio_format* fmt) {}

struct buffer_share* buffer_share_create(unsigned int buf_sz) {
  return NULL;
}

void buffer_share_destroy(struct buffer_share* mix) {}

int buffer_share_offset_update(struct buffer_share* mix,
                               unsigned int id,
                               unsigned int frames) {
  return 0;
}

unsigned int buffer_share_get_new_write_point(struct buffer_share* mix) {
  return buffer_share_get_new_write_point_ret;
}

int buffer_share_add_id(struct buffer_share* mix, unsigned int id, void* data) {
  return 0;
}

int buffer_share_rm_id(struct buffer_share* mix, unsigned int id) {
  return 0;
}

unsigned int buffer_share_id_offset(const struct buffer_share* mix,
                                    unsigned int id) {
  return 0;
}
void ewma_power_init(struct ewma_power* ewma,
                     snd_pcm_format_t fmt,
                     unsigned int rate) {}

void ewma_power_calculate(struct ewma_power* ewma,
                          const int16_t* buf,
                          unsigned int channels,
                          unsigned int size) {
  unsigned val = 0;
  for (unsigned int i = 0; i < size * channels; i += channels) {
    val += buf[i];
  }
  (void)val;
}

void cras_system_state_stream_added(enum CRAS_STREAM_DIRECTION direction,
                                    enum CRAS_CLIENT_TYPE client_type) {}

void cras_system_state_stream_removed(enum CRAS_STREAM_DIRECTION direction,
                                      enum CRAS_CLIENT_TYPE client_type) {}

int cras_system_aec_on_dsp_supported() {
  return 0;
}

int cras_system_ns_on_dsp_supported() {
  return 0;
}

int cras_system_agc_on_dsp_supported() {
  return 0;
}

int cras_server_metrics_stream_create(
    const struct cras_rstream_config* config) {
  return 0;
}

int cras_server_metrics_stream_create_failure(
    enum CRAS_STREAM_CREATE_ERROR code) {
  return 0;
}

int cras_server_metrics_stream_destroy(const struct cras_rstream* stream) {
  return 0;
}

#define FAKE_CRAS_APM_PTR reinterpret_cast<struct cras_apm*>(0x99)
struct cras_stream_apm* cras_stream_apm_create(uint64_t effects) {
  return fake_stream_apm;
}
struct cras_apm* cras_stream_apm_get_active(struct cras_stream_apm* stream,
                                            const struct cras_iodev* idev) {
  cras_stream_apm_get_active_stream = stream;
  cras_stream_apm_get_active_idev = idev;
  return FAKE_CRAS_APM_PTR;
}
int cras_stream_apm_destroy(struct cras_stream_apm* stream) {
  return 0;
}
uint64_t cras_stream_apm_get_effects(struct cras_stream_apm* stream) {
  return APM_ECHO_CANCELLATION;
}
struct cras_apm* cras_stream_apm_get(struct cras_stream_apm* stream,
                                     const struct cras_iodev* idev) {
  return NULL;
}
struct cras_audio_format* cras_stream_apm_get_format(struct cras_apm* apm) {
  return NULL;
}
}

// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <sys/shm.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_messages.h"
#include "cras_rstream.h"
#include "cras_shm.h"
}

namespace {

class RstreamTestSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      fmt_.format = SND_PCM_FORMAT_S16_LE;
      fmt_.frame_rate = 48000;
      fmt_.num_channels = 2;
    }

    struct cras_audio_format fmt_;
};

TEST_F(RstreamTestSuite, InvalidDirection) {
  struct cras_rstream *s;
  int rc;

  rc = cras_rstream_create(555,
      CRAS_STREAM_TYPE_DEFAULT,
      (enum CRAS_STREAM_DIRECTION)66,
      &fmt_,
      4096,
      1024,
      2048,
      0,
      NULL,
      &s);
  EXPECT_NE(0, rc);
}

TEST_F(RstreamTestSuite, InvalidBufferSize) {
  struct cras_rstream *s;
  int rc;

  rc = cras_rstream_create(555,
      CRAS_STREAM_TYPE_DEFAULT,
      CRAS_STREAM_OUTPUT,
      &fmt_,
      3,
      1024,
      2048,
      0,
      NULL,
      &s);
  EXPECT_NE(0, rc);
}

TEST_F(RstreamTestSuite, InvalidCallbackThreshold) {
  struct cras_rstream *s;
  int rc;

  rc = cras_rstream_create(555,
      CRAS_STREAM_TYPE_DEFAULT,
      CRAS_STREAM_OUTPUT,
      &fmt_,
      4096,
      3,
      2048,
      0,
      NULL,
      &s);
  EXPECT_NE(0, rc);
}

TEST_F(RstreamTestSuite, InvalidStreamPointer) {
  int rc;

  rc = cras_rstream_create(555,
      CRAS_STREAM_TYPE_DEFAULT,
      CRAS_STREAM_OUTPUT,
      &fmt_,
      4096,
      1024,
      2048,
      0,
      NULL,
      NULL);
  EXPECT_NE(0, rc);
}

TEST_F(RstreamTestSuite, CreateOutput) {
  struct cras_rstream *s;
  struct cras_audio_format fmt_ret;
  struct cras_audio_shm *shm_ret;
  struct cras_audio_shm shm_mapped;
  int rc, key_ret, shmid;
  size_t shm_size;

  rc = cras_rstream_create(555,
      CRAS_STREAM_TYPE_DEFAULT,
      CRAS_STREAM_OUTPUT,
      &fmt_,
      4096,
      1024,
      2048,
      0,
      NULL,
      &s);
  EXPECT_EQ(0, rc);
  EXPECT_NE((void *)NULL, s);
  EXPECT_EQ(4096, cras_rstream_get_buffer_size(s));
  EXPECT_EQ(1024, cras_rstream_get_cb_threshold(s));
  EXPECT_EQ(2048, cras_rstream_get_min_cb_level(s));
  EXPECT_EQ(CRAS_STREAM_TYPE_DEFAULT, cras_rstream_get_type(s));
  EXPECT_EQ(CRAS_STREAM_OUTPUT, cras_rstream_get_direction(s));
  EXPECT_NE((void *)NULL, cras_rstream_get_shm(s));
  rc = cras_rstream_get_format(s, &fmt_ret);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, memcmp(&fmt_ret, &fmt_, sizeof(fmt_)));

  // Check if shm is really set up.
  shm_ret = cras_rstream_get_shm(s);
  ASSERT_NE((void *)NULL, shm_ret);
  key_ret = cras_rstream_get_shm_key(s);
  shm_size = cras_rstream_get_total_shm_size(s);
  EXPECT_GT(shm_size, 4096);
  shmid = shmget(key_ret, shm_size, 0600);
  EXPECT_GE(shmid, 0);
  shm_mapped.area = (struct cras_audio_shm_area *)shmat(shmid, NULL, 0);
  EXPECT_NE((void *)NULL, shm_mapped.area);
  cras_shm_copy_shared_config(&shm_mapped);
  EXPECT_EQ(cras_shm_used_size(&shm_mapped), cras_shm_used_size(shm_ret));
  shmdt(shm_mapped.area);

  cras_rstream_destroy(s);
}

TEST_F(RstreamTestSuite, CreateInput) {
  struct cras_rstream *s;
  struct cras_audio_format fmt_ret;
  struct cras_audio_shm *shm_ret;
  struct cras_audio_shm shm_mapped;
  int rc, key_ret, shmid;
  size_t shm_size;

  rc = cras_rstream_create(555,
      CRAS_STREAM_TYPE_DEFAULT,
      CRAS_STREAM_INPUT,
      &fmt_,
      4096,
      1024,
      2048,
      0,
      NULL,
      &s);
  EXPECT_EQ(0, rc);
  EXPECT_NE((void *)NULL, s);
  EXPECT_EQ(4096, cras_rstream_get_buffer_size(s));
  EXPECT_EQ(1024, cras_rstream_get_cb_threshold(s));
  EXPECT_EQ(2048, cras_rstream_get_min_cb_level(s));
  EXPECT_EQ(CRAS_STREAM_TYPE_DEFAULT, cras_rstream_get_type(s));
  EXPECT_EQ(CRAS_STREAM_INPUT, cras_rstream_get_direction(s));
  EXPECT_NE((void *)NULL, cras_rstream_get_shm(s));
  rc = cras_rstream_get_format(s, &fmt_ret);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, memcmp(&fmt_ret, &fmt_, sizeof(fmt_)));

  // Check if shm is really set up.
  shm_ret = cras_rstream_get_shm(s);
  ASSERT_NE((void *)NULL, shm_ret);
  key_ret = cras_rstream_get_shm_key(s);
  shm_size = cras_rstream_get_total_shm_size(s);
  EXPECT_GT(shm_size, 4096);
  shmid = shmget(key_ret, shm_size, 0600);
  EXPECT_GE(shmid, 0);
  shm_mapped.area = (struct cras_audio_shm_area *)shmat(shmid, NULL, 0);
  EXPECT_NE((void *)NULL, shm_mapped.area);
  cras_shm_copy_shared_config(&shm_mapped);
  EXPECT_EQ(cras_shm_used_size(&shm_mapped), cras_shm_used_size(shm_ret));
  shmdt(shm_mapped.area);

  cras_rstream_destroy(s);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/* stubs */
extern "C" {

int cras_rclient_send_message(const struct cras_rclient *client,
            const struct cras_message *msg)
{
  return 0;
}

}

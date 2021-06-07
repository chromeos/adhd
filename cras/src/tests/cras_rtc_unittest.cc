/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>
#include <stdio.h>

extern "C" {
#include "cras_rtc.h"
}

namespace {

TEST(CrasRtcSuite, BasicRTC) {
  struct cras_rstream in_stream, out_stream;
  struct cras_iodev in_dev, out_dev;

  in_stream.cb_threshold = 480;
  in_stream.direction = CRAS_STREAM_INPUT;
  in_stream.client_type = CRAS_CLIENT_TYPE_CHROME;
  in_stream.stream_type = CRAS_STREAM_TYPE_DEFAULT;

  out_stream.cb_threshold = 480;
  out_stream.direction = CRAS_STREAM_OUTPUT;
  out_stream.client_type = CRAS_CLIENT_TYPE_CHROME;
  out_stream.stream_type = CRAS_STREAM_TYPE_DEFAULT;

  in_dev.info.idx = 100;
  out_dev.info.idx = 101;

  cras_rtc_add_stream(&in_stream, &in_dev);
  cras_rtc_add_stream(&out_stream, &out_dev);

  EXPECT_EQ(in_stream.stream_type, CRAS_STREAM_TYPE_VOICE_COMMUNICATION);
  EXPECT_EQ(out_stream.stream_type, CRAS_STREAM_TYPE_VOICE_COMMUNICATION);

  cras_rtc_remove_stream(&in_stream, 100);
  cras_rtc_remove_stream(&out_stream, 101);
}

TEST(CrasRtcSuite, BasicNoRTC) {
  struct cras_rstream in_stream, out_stream;
  struct cras_iodev in_dev, out_dev;

  in_stream.cb_threshold = 480;
  in_stream.direction = CRAS_STREAM_INPUT;
  in_stream.client_type = CRAS_CLIENT_TYPE_CHROME;
  in_stream.stream_type = CRAS_STREAM_TYPE_DEFAULT;

  // cb_threshold != 480
  out_stream.cb_threshold = 512;
  out_stream.direction = CRAS_STREAM_OUTPUT;
  out_stream.client_type = CRAS_CLIENT_TYPE_CHROME;
  out_stream.stream_type = CRAS_STREAM_TYPE_DEFAULT;

  in_dev.info.idx = 100;
  out_dev.info.idx = 101;

  cras_rtc_add_stream(&in_stream, &in_dev);
  cras_rtc_add_stream(&out_stream, &out_dev);

  EXPECT_EQ(in_stream.stream_type, CRAS_STREAM_TYPE_DEFAULT);
  EXPECT_EQ(out_stream.stream_type, CRAS_STREAM_TYPE_DEFAULT);

  // Device idx < MAX_SPECIAL_DEVICE_IDX
  cras_rtc_remove_stream(&out_stream, 101);
  out_stream.cb_threshold = 480;
  out_dev.info.idx = 1;
  cras_rtc_add_stream(&out_stream, &out_dev);
  EXPECT_EQ(in_stream.stream_type, CRAS_STREAM_TYPE_DEFAULT);
  EXPECT_EQ(out_stream.stream_type, CRAS_STREAM_TYPE_DEFAULT);

  // client type != CRAS_CLIENT_TYPE_CHROME or CRAS_CLIENT_TYPE_LACROS
  cras_rtc_remove_stream(&out_stream, 1);
  out_stream.cb_threshold = 480;
  out_stream.client_type = CRAS_CLIENT_TYPE_CROSVM;
  out_dev.info.idx = 101;
  cras_rtc_add_stream(&out_stream, &out_dev);
  EXPECT_EQ(in_stream.stream_type, CRAS_STREAM_TYPE_DEFAULT);
  EXPECT_EQ(out_stream.stream_type, CRAS_STREAM_TYPE_DEFAULT);

  cras_rtc_remove_stream(&in_stream, 100);
  cras_rtc_remove_stream(&out_stream, 101);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

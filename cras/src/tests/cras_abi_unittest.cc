/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>

extern "C" {
#include "cras_client.c"
#include "cras_client.h"

inline int libcras_unsupported_func(struct libcras_client* client) {
  CHECK_VERSION(client, INT_MAX);
  return 0;
}
}

namespace {

TEST(CrasAbiTestSuite, CheckUnsupportedFunction) {
  auto* client = libcras_client_create();
  EXPECT_NE((void*)NULL, client);
  EXPECT_EQ(-ENOSYS, libcras_unsupported_func(client));
  libcras_client_destroy(client);
}

TEST(CrasAbiTestSuite, BasicStream) {
  auto* client = libcras_client_create();
  EXPECT_NE((void*)NULL, client);
  auto* stream = libcras_stream_params_create();
  EXPECT_NE((void*)NULL, stream);
  /* Returns timeout because there is no real CRAS server in unittest. */
  EXPECT_EQ(-ETIMEDOUT, libcras_client_connect_timeout(client, 0));
  EXPECT_EQ(0, libcras_client_run_thread(client));
  EXPECT_EQ(0, libcras_stream_params_set(
                   stream, CRAS_STREAM_INPUT, 480, 480,
                   CRAS_STREAM_TYPE_DEFAULT, CRAS_CLIENT_TYPE_TEST, 0, NULL,
                   NULL, NULL, NULL, 48000, SND_PCM_FORMAT_S16, 2));
  cras_stream_id_t id;
  /* Fails to add a stream because the stream callback is not set. */
  EXPECT_EQ(-EINVAL, libcras_client_add_pinned_stream(client, 0, &id, stream));
  /* Fails to set a stream volume because the stream is not added. */
  EXPECT_EQ(-EINVAL, libcras_client_set_stream_volume(client, id, 1.0));
  EXPECT_EQ(0, libcras_client_rm_stream(client, id));
  EXPECT_EQ(0, libcras_client_stop(client));
  libcras_stream_params_destroy(stream);
  libcras_client_destroy(client);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

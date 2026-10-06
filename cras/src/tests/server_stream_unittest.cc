// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <vector>

#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_server.h"
#include "cras/src/server/server_stream.h"
#include "cras/src/server/stream_list.h"
#include "third_party/utlist/utlist.h"

//
// Stubs :(
//
static int stream_callback_noop(struct cras_rstream* rstream) {
  return 0;
}

static int stream_create_noop(struct cras_rstream_config* config,
                              struct cras_rstream** stream_out) {
  return 0;
}

static void stream_destroy_noop(struct cras_rstream* stream) {}

//
// Fake cras_system_add_task
//
struct task {
  void (*callback)(void* data);
  void* callback_data;
};
std::vector<task> tasks;

extern "C" int cras_system_add_task(void (*callback)(void* data),
                                    void* callback_data) {
  tasks.push_back({callback, callback_data});
  return 0;
}

void run_all_pending_tasks() {
  for (auto& task : tasks) {
    task.callback(task.callback_data);
  }
  tasks.clear();
}

namespace {

// For b/323765262.
TEST(ServerStream, CreateDestroyRace) {
  struct stream_list* sl = stream_list_create(
      stream_callback_noop, stream_callback_noop, stream_create_noop,
      stream_destroy_noop, stream_callback_noop, nullptr);

  const unsigned int dev_idx = 1234;
  server_stream_create(sl, SERVER_STREAM_ECHO_REF, dev_idx, nullptr, 0, false,
                       DEFAULT_SERVER_STREAM_BLOCK_SIZE);
  server_stream_destroy(sl, SERVER_STREAM_ECHO_REF, dev_idx);
  run_all_pending_tasks();

  stream_list_destroy(sl);
}

TEST(ServerStream, FindByTypeChecksClientType) {
  struct stream_list* sl = stream_list_create(
      stream_callback_noop, stream_callback_noop, stream_create_noop,
      stream_destroy_noop, stream_callback_noop, nullptr);

  const unsigned int dev_idx = 0;
  server_stream_create(sl, SERVER_STREAM_SIDETONE_OUTPUT, dev_idx, nullptr, 0,
                       false, DEFAULT_SERVER_STREAM_BLOCK_SIZE);

  struct cras_rstream fake_client_stream = {};
  fake_client_stream.stream_id = cras_get_stream_id(
      SERVER_STREAM_CLIENT_ID, SERVER_STREAM_SIDETONE_OUTPUT);
  fake_client_stream.client_type = CRAS_CLIENT_TYPE_UNKNOWN;

  struct cras_rstream real_server_stream = {};
  real_server_stream.stream_id = cras_get_stream_id(
      SERVER_STREAM_CLIENT_ID, SERVER_STREAM_SIDETONE_OUTPUT);
  real_server_stream.client_type = CRAS_CLIENT_TYPE_SERVER_STREAM;

  struct cras_rstream* all_streams = nullptr;
  DL_APPEND(all_streams, &fake_client_stream);
  DL_APPEND(all_streams, &real_server_stream);

  EXPECT_EQ(
      &real_server_stream,
      server_stream_find_by_type(all_streams, SERVER_STREAM_SIDETONE_OUTPUT));

  server_stream_destroy(sl, SERVER_STREAM_SIDETONE_OUTPUT, dev_idx);
  run_all_pending_tasks();
  stream_list_destroy(sl);
}

}  // namespace

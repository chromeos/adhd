// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/server/cras_thread.h"
#include "gtest/gtest.h"

TEST(CrasThread, Checks) {
  // Both contexts are not allowed before initialization.
  EXPECT_EXIT(checked_main_ctx(), testing::KilledBySignal(SIGABRT),
              "main_ctx_allowed");
  EXPECT_EXIT(checked_audio_ctx(), testing::KilledBySignal(SIGABRT),
              "audio_ctx_allowed");

  cras_thread_init_main();
  // Both contexts are allowed after initialization.
  checked_main_ctx()->test_num = 1;
  checked_audio_ctx()->test_num = 2;

  pthread_t child_tid = {};
  auto audio_thread = [](void* data) -> void* {
    // audio_ctx allowed in the audio thread.
    EXPECT_EQ(checked_audio_ctx()->test_num, 2);
    return nullptr;
  };
  ASSERT_EQ(
      cras_thread_create_audio(&child_tid, nullptr, audio_thread, nullptr), 0);
  EXPECT_NE(pthread_self(), child_tid);
  // main_ctx allowed after creating the audio thread.
  EXPECT_EQ(checked_main_ctx()->test_num, 1);
  // audio_ctx not allowed after creating the audio thread.
  EXPECT_EXIT(checked_audio_ctx(), testing::KilledBySignal(SIGABRT),
              "audio_ctx_allowed");
}

// Implementation note: this is not merged into CrasThread.Checks because
// of gtest limitations on death tests and threads:
// https://github.com/google/googletest/blob/main/docs/advanced.md#death-test-styles
TEST(CrasThread, CheckBadAudioThread) {
  cras_thread_init_main();
  checked_main_ctx()->test_num = 1;
  checked_audio_ctx()->test_num = 2;

  auto audio_thread = [](void* data) -> void* {
    // bad main_ctx access.
    checked_main_ctx();
    return nullptr;
  };
  auto spawn_bad_audio_thread = [&]() {
    pthread_t child_tid = {};
    ASSERT_EQ(
        cras_thread_create_audio(&child_tid, nullptr, audio_thread, nullptr),
        0);
    pthread_join(child_tid, nullptr);
  };

  EXPECT_EXIT(spawn_bad_audio_thread(), testing::KilledBySignal(SIGABRT),
              "main_ctx_allowed");
}

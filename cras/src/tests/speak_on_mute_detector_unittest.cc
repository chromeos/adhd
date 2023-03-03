// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <time.h>

#include "cras/src/server/speak_on_mute_detector.h"

namespace {

class SpeakOnMuteDetector : public ::testing::Test {
 protected:
  bool addActivity(bool detected, const struct timespec& when) {
    return speak_on_mute_detector_add_voice_activity_at(&d, detected, &when);
  }

  struct speak_on_mute_detector d;
};

TEST_F(SpeakOnMuteDetector, Window3Threshold3NoRateLimit) {
  const struct speak_on_mute_detector_config cfg = {
      .detection_window_size = 3,
      .detection_threshold = 3,
      .rate_limit_duration = {.tv_sec = 0, .tv_nsec = 0},
  };
  ASSERT_EQ(speak_on_mute_detector_init(&d, &cfg), 0);

  struct timespec now = {
      .tv_sec = 1,
      .tv_nsec = 0,
  };

  EXPECT_FALSE(addActivity(true, now));
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_TRUE(addActivity(true, now));
  EXPECT_TRUE(addActivity(true, now));
  EXPECT_TRUE(addActivity(true, now));

  speak_on_mute_detector_reset(&d);
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_TRUE(addActivity(true, now));
  EXPECT_TRUE(addActivity(true, now));
  EXPECT_TRUE(addActivity(true, now));
}

TEST_F(SpeakOnMuteDetector, Window3Threshold3) {
  const struct speak_on_mute_detector_config cfg = {
      .detection_window_size = 3,
      .detection_threshold = 3,
      .rate_limit_duration = {.tv_sec = 1, .tv_nsec = 0},
  };
  ASSERT_EQ(speak_on_mute_detector_init(&d, &cfg), 0);

  struct timespec now = {
      .tv_sec = 1,
      .tv_nsec = 0,
  };

  EXPECT_FALSE(addActivity(true, now));
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_FALSE(addActivity(false, now));
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_FALSE(addActivity(true, now));
  // Consecutive 3 true values.
  EXPECT_TRUE(addActivity(true, now));
  // Rate limited.
  EXPECT_FALSE(addActivity(true, now));
  now.tv_sec += 1;
  EXPECT_TRUE(addActivity(true, now));
  // Rate limited again.
  EXPECT_FALSE(addActivity(true, now));

  speak_on_mute_detector_reset(&d);
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_FALSE(addActivity(false, now));
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_TRUE(addActivity(true, now));
}

TEST_F(SpeakOnMuteDetector, Window5Threshold3) {
  const struct speak_on_mute_detector_config cfg = {
      .detection_window_size = 5,
      .detection_threshold = 3,
      .rate_limit_duration = {.tv_sec = 1, .tv_nsec = 0},
  };
  ASSERT_EQ(speak_on_mute_detector_init(&d, &cfg), 0);

  struct timespec now = {
      .tv_sec = 1,
      .tv_nsec = 0,
  };

  EXPECT_FALSE(addActivity(true, now));
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_FALSE(addActivity(false, now));
  // 3 true in the last 5 values.
  EXPECT_TRUE(addActivity(true, now));
  now.tv_sec += 1;
  EXPECT_FALSE(addActivity(false, now));
  EXPECT_FALSE(addActivity(false, now));
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_TRUE(addActivity(true, now));
  now.tv_sec += 1;
  EXPECT_FALSE(addActivity(false, now));

  speak_on_mute_detector_reset(&d);
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_FALSE(addActivity(true, now));
  EXPECT_FALSE(addActivity(false, now));
  EXPECT_TRUE(addActivity(true, now));
}

TEST_F(SpeakOnMuteDetector, Bounds) {
  const int max_window_size = 63;

  struct speak_on_mute_detector_config cfg = {
      .detection_window_size = max_window_size + 1,
      .detection_threshold = max_window_size + 1,
      .rate_limit_duration = {.tv_sec = 1, .tv_nsec = 0},
  };

  // Should reject: window size too large.
  ASSERT_EQ(speak_on_mute_detector_init(&d, &cfg), -EINVAL);

  cfg = {
      .detection_window_size = max_window_size,
      .detection_threshold = max_window_size,
      .rate_limit_duration = {.tv_sec = 0, .tv_nsec = 0},
  };
  ASSERT_EQ(speak_on_mute_detector_init(&d, &cfg), 0);

  struct timespec now = {
      .tv_sec = 1,
      .tv_nsec = 0,
  };
  for (int i = 1; i < max_window_size; i++) {
    EXPECT_FALSE(addActivity(true, now));
  }
  EXPECT_TRUE(addActivity(true, now));
  EXPECT_TRUE(addActivity(true, now));
}

}  // namespace

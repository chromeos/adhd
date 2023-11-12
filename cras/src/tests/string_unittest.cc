// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <gtest/gtest.h>

#include "cras/src/common/cras_string.h"

namespace {

TEST(String, Strerror) {
  EXPECT_STREQ(cras_strerror(ENOENT), "No such file or directory");
  EXPECT_STREQ(cras_strerror(65536), "Unknown error 65536");
}

TEST(String, HasPrefix) {
  EXPECT_TRUE(str_has_prefix("string", "str"));
  EXPECT_FALSE(str_has_prefix("string", "ring"));
  EXPECT_FALSE(str_has_prefix("str", "string"));
}

TEST(String, StrEquals) {
  EXPECT_TRUE(str_equals("string", "string"));
  EXPECT_FALSE(str_equals("str", "string"));
  EXPECT_FALSE(str_equals("string", "str"));
  EXPECT_FALSE(str_equals(nullptr, "string"));
  EXPECT_FALSE(str_equals("str", nullptr));
  EXPECT_FALSE(str_equals("", "string"));
  EXPECT_FALSE(str_equals("str", ""));
  EXPECT_TRUE(str_equals("", ""));
  EXPECT_FALSE(str_equals(nullptr, nullptr));
}

TEST(String, StrEqualsBounded) {
  const char* str_literal = "string";
  char str[8];
  strncpy(str, str_literal, sizeof(str));
  char unbounded_str[8] = {'s', 't', 'r', 'i',
                           'n', 'g', '#', '$'};  // garbage chars

  EXPECT_TRUE(str_equals_bounded(str, str_literal, sizeof(str)));
  EXPECT_FALSE(str_equals_bounded(str, str_literal, strlen(str_literal)));
  EXPECT_FALSE(
      str_equals_bounded(unbounded_str, str_literal, sizeof(unbounded_str)));
  EXPECT_FALSE(
      str_equals_bounded(unbounded_str, str_literal, strlen(str_literal)));
  EXPECT_FALSE(str_equals_bounded(str, unbounded_str, sizeof(str)));
  EXPECT_FALSE(str_equals_bounded(str, unbounded_str, strlen(str_literal)));
  EXPECT_FALSE(str_equals_bounded(nullptr, str_literal, strlen(str_literal)));
  EXPECT_FALSE(str_equals_bounded(str, nullptr, sizeof(str)));
  EXPECT_FALSE(str_equals_bounded(nullptr, nullptr, strlen(str_literal)));
}
}  //  namespace

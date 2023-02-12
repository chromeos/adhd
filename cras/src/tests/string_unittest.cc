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

}  //  namespace

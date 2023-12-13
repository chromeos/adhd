// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/common/check.h"
#include "gtest/gtest.h"

TEST(CrasCheck, Pass) {
  CRAS_CHECK(true);
}

TEST(CrasCheck, Fail) {
  ASSERT_DEATH(CRAS_CHECK(false), "");
}

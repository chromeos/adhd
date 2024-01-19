// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>

#include "cras/common/rust_common.h"
#include "gtest/gtest.h"

TEST(Pseudonymization, Bindings) {
  EXPECT_NE(pseudonymize_stable_id(0), 0);
  EXPECT_NE(pseudonymize_stable_id(1), 1);
}

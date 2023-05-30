// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>

#include "cras/src/server/rust/include/pseudonymization.h"
#include "gtest/gtest.h"

TEST(Pseudonymization, Bindings) {
  const char* env = "CRAS_PSEUDONYMIZATION_SALT";
  uint32_t salt;
  ASSERT_EQ(pseudonymize_salt_get_from_env(&salt), 0)
      << "environment unset; pass";

  setenv(env, "1", /*replace=*/1);
  ASSERT_EQ(pseudonymize_salt_get_from_env(&salt), 0)
      << "environment set to 1; pass";
  ASSERT_EQ(salt, 1u);

  setenv(env, "4294967295", /*replace=*/1);
  ASSERT_EQ(pseudonymize_salt_get_from_env(&salt), 0)
      << "environment set to 4294967295; pass";
  ASSERT_EQ(salt, 4294967295u);

  setenv(env, "4294967296", /*replace=*/1);
  ASSERT_EQ(pseudonymize_salt_get_from_env(&salt), -EINVAL)
      << "environment set to 4294967296; too large";

  setenv(env, "-1", /*replace=*/1);
  ASSERT_EQ(pseudonymize_salt_get_from_env(&salt), -EINVAL)
      << "environment set to -1; negative is invalid";

  setenv(env, "aaa", /*replace=*/1);
  ASSERT_EQ(pseudonymize_salt_get_from_env(&salt), -EINVAL)
      << "environment set to aaa; invalid";

  setenv(env, "", /*replace=*/1);
  ASSERT_EQ(pseudonymize_salt_get_from_env(&salt), -EINVAL)
      << "environment set but empty; invalid";
}

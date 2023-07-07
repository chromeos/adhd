// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <errno.h>

#include "cras/src/server/rust/include/cras_fra.h"
#include "gtest/gtest.h"

TEST(FRA, Fralog) {
  struct cras_fra_kv_t context0[] = {};
  struct cras_fra_kv_t context1[] = {{"key1", "value1"}};

  // These will be tested with asan and ubsan.
  fralog(PeripheralsUsbSoundCard,
         sizeof(context0) / sizeof(struct cras_fra_kv_t), context0);
  fralog(PeripheralsUsbSoundCard,
         sizeof(context1) / sizeof(struct cras_fra_kv_t), context1);
}

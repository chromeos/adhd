// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "cras/src/common/cras_log.h"

namespace {

TEST(CrasLog, tlsprintf_buffer_over_run) {
  for (int i = 0; i < 24; i++) {
    tlsprintf("very long string %-1000d", i);
  }
}

}  // namespace

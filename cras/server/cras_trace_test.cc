// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/server/cras_trace.h"
#include "gtest/gtest.h"

TEST(CrasTrace, Smoke) {
  { TRACE_EVENT(audio, __func__); }
  {
    TRACE_EVENT_DATA(audio, __func__, PERCETTO_I(99),
                     PERCETTO_S("random string"));
  }
}

int main(int argc, char** argv) {
  cras_trace_init();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

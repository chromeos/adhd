// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>

#include "cras/src/dsp/rust/headers/dcblock.h"
#include "gtest/gtest.h"

// This test should be run with address sanitizer to test out the memory
// of dcblock is free out and no memory errors occur.
TEST(Foo, Bar) {
  struct dcblock* p = dcblock_new();

  dcblock_free(p);
}
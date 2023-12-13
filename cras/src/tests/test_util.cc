// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/tests/test_util.hh"

#include "cras/common/check.h"

const char* test_tmpdir() {
  const char* dir = getenv("TEST_TMPDIR");
  CRAS_CHECK(dir != nullptr);
  return dir;
}

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/server/cras_trace.h"

PERCETTO_CATEGORY_DEFINE(CRAS_PERCETTO_CATEGORIES);

int cras_trace_init() {
  return PERCETTO_INIT(PERCETTO_CLOCK_DONT_CARE);
}

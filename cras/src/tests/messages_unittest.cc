// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include <cras_messages.h>
}

#include <gtest/gtest.h>

static_assert(sizeof(cras_request_floop::tag) >= sizeof(void*),
              "cras_request_floop::tag is not big enough to store a pointer");
static_assert(sizeof(cras_client_request_floop_ready::tag) >= sizeof(void*),
              "cras_client_request_floop_ready::tag is not big enough to store "
              "a pointer");

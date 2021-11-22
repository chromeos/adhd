// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "cras_floop_iodev.c"
}

#include <gtest/gtest.h>

TEST(FlexibleLoopback, PointerArithmetic) {
  struct flexible_loopback floop;
  EXPECT_EQ(&floop, const_pair_to_floop(&floop.pair));
  EXPECT_EQ(&floop, input_to_floop(&floop.pair.input));
  EXPECT_EQ(&floop, output_to_floop(&floop.pair.output));
}

TEST(FlexibleLoopback, CrasFloopPairMatchOutputStream) {
  struct flexible_loopback floop = {
      .params = {.client_types_mask = 1 << CRAS_CLIENT_TYPE_CHROME}};

  struct cras_rstream test_stream = {.client_type = CRAS_CLIENT_TYPE_TEST};
  struct cras_rstream chrome_stream = {.client_type = CRAS_CLIENT_TYPE_CHROME};

  EXPECT_FALSE(cras_floop_pair_match_output_stream(&floop.pair, &test_stream))
      << "should not match: different mask, floop is not active";
  EXPECT_FALSE(cras_floop_pair_match_output_stream(&floop.pair, &chrome_stream))
      << "should not match: floop is not active";

  floop.input_active = true;

  EXPECT_FALSE(cras_floop_pair_match_output_stream(&floop.pair, &test_stream))
      << "should not match: different mask";
  EXPECT_TRUE(cras_floop_pair_match_output_stream(&floop.pair, &chrome_stream))
      << "should match: floop active and matching mask";
}

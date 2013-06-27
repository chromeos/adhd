/* Copyright (c) 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <gtest/gtest.h>

extern "C" {
  #include "cras_hfp_info.h"
}

static struct hfp_info *info;
static struct cras_iodev dev;

namespace {

TEST(HfpInfo, AddRmDev) {
  info = hfp_info_create();
  ASSERT_NE(info, (void *)NULL);

  dev.direction = CRAS_STREAM_OUTPUT;

  /* Test add dev */
  ASSERT_EQ(0, hfp_info_add_iodev(info, &dev));
  ASSERT_TRUE(hfp_info_has_iodev(info));

  /* Test remove dev */
  ASSERT_EQ(0, hfp_info_rm_iodev(info, &dev));
  ASSERT_FALSE(hfp_info_has_iodev(info));

  hfp_info_destroy(info);
}

TEST(HfpInfo, AddRmDevInvalid) {
  info = hfp_info_create();
  ASSERT_NE(info, (void *)NULL);

  dev.direction = CRAS_STREAM_OUTPUT;

  /* Remove an iodev which doesn't exist */
  ASSERT_NE(0, hfp_info_rm_iodev(info, &dev));

  /* Adding an iodev twice returns error code */
  ASSERT_EQ(0, hfp_info_add_iodev(info, &dev));
  ASSERT_NE(0, hfp_info_add_iodev(info, &dev));

  hfp_info_destroy(info);
}

} // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

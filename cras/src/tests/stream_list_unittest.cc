// Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <sys/shm.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_rstream.h"
#include "stream_list.h"
}

namespace {

static unsigned int add_called;
static int added_cb(struct cras_rstream *rstream) {
  add_called++;
  return 0;
}

static unsigned int rm_called;
static struct cras_rstream *rmed_stream;
static int removed_cb(struct cras_rstream *rstream) {
  rm_called++;
  rmed_stream = rstream;
  return 0;
}

static void reset_test_data() {
  add_called = 0;
  rm_called = 0;
}

TEST(StreamList, AddRemove) {
  struct stream_list *l;
  struct cras_rstream s1;

  reset_test_data();
  l = stream_list_create(added_cb, removed_cb);
  memset(&s1, 0, sizeof(s1));
  s1.stream_id = 0xf00;
  stream_list_add(l, &s1);
  EXPECT_EQ(1, add_called);
  EXPECT_EQ(&s1, stream_list_rm(l, 0xf00));
  EXPECT_EQ(1, rm_called);
  EXPECT_EQ(&s1, rmed_stream);
  stream_list_destroy(l);
}

} // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

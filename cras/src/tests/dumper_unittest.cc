// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <syslog.h>

#include "cras/src/common/dumper.h"

namespace {

TEST(DumperTest, SyslogDumper) {
  struct dumper* dumper = syslog_dumper_create(LOG_WARNING);
  dumpf(dumper, "hello %d", 1);
  dumpf(dumper, "world %d\n123", 2);
  dumpf(dumper, "456\n");
  // The following should appear in syslog:
  // dumper_unittest: hello 1world 2
  // dumper_unittest: 123456
  syslog_dumper_free(dumper);
}

TEST(DumperTest, MemDumper) {
  struct dumper* dumper = mem_dumper_create();
  char* buf;
  int size, i;

  mem_dumper_get(dumper, &buf, &size);
  EXPECT_STREQ("", buf);

  dumpf(dumper, "hello %d\n", 1);
  mem_dumper_get(dumper, &buf, &size);
  EXPECT_STREQ("hello 1\n", buf);
  EXPECT_EQ(8, size);

  dumpf(dumper, "world %d", 2);
  mem_dumper_get(dumper, &buf, &size);
  EXPECT_STREQ("hello 1\nworld 2", buf);
  EXPECT_EQ(15, size);

  mem_dumper_clear(dumper);
  mem_dumper_get(dumper, &buf, &size);
  EXPECT_STREQ("", buf);
  EXPECT_EQ(0, size);

  // Test if format string is kept.
  for (i = 0; i < 10; i++) {
    dumpf(dumper, "%s", "1234567890");
  }
  mem_dumper_get(dumper, &buf, &size);
  EXPECT_EQ(100, strlen(buf));
  EXPECT_EQ(100, size);
  mem_dumper_consume(dumper, size);

  for (i = 0; i < 1000; i++) {
    dumpf(dumper, "a");
  }
  mem_dumper_get(dumper, &buf, &size);
  EXPECT_EQ(1000, strlen(buf));
  EXPECT_EQ(1000, size);

  mem_dumper_free(dumper);
}

}  //  namespace

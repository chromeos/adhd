// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "cras/src/common/cras_checksum.h"
#include "cras_util.h"

namespace {

struct TestCase {
  const char* input;
  uint32_t output;
};

static TestCase test_case[] = {
    // The answers can be obtained by a command like "echo -n a | cksum"
    {"", 4294967295U},
    {"a", 1220704766U},
    {"12345678901234567890", 970143720U},
};

TEST(ChecksumTest, All) {
  for (size_t i = 0; i < ARRAY_SIZE(test_case); i++) {
    const char* input = test_case[i].input;
    uint32_t output = test_case[i].output;
    EXPECT_EQ(output, crc32_checksum((unsigned char*)input, strlen(input)));
  }
}

}  //  namespace

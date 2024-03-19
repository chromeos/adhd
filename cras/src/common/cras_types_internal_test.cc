// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <string_view>

#include "cras/src/common/cras_types_internal.h"
#include "gtest/gtest.h"

TEST(CrasTypesInternal, PrintCrasStreamActiveEffects) {
  {
    char* buf;
    size_t bufsize;
    FILE* f = open_memstream(&buf, &bufsize);
    ASSERT_TRUE(f);
    print_cras_stream_active_effects(
        f, CRAS_STREAM_ACTIVE_EFFECT(AE_NEGATE | AE_NOISE_CANCELLATION));
    ASSERT_EQ(fclose(f), 0);
    EXPECT_EQ(std::string_view(buf, bufsize), " negate noise_cancellation");
    free(buf);
  }

  {
    char* buf;
    size_t bufsize;
    FILE* f = open_memstream(&buf, &bufsize);
    ASSERT_TRUE(f);
    print_cras_stream_active_effects(f, CRAS_STREAM_ACTIVE_EFFECT(0));
    ASSERT_EQ(fclose(f), 0);
    EXPECT_EQ(std::string_view(buf, bufsize), " none");
    free(buf);
  }
}

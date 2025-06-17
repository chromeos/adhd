// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <string_view>

#include "cras/common/string.h"
#include "gtest/gtest.h"

using namespace std::literals::string_view_literals;

std::string escape_string_view(std::string_view sv) {
  char* cstring = escape_string(sv.data(), sv.length());
  std::string out(cstring);
  free(cstring);
  return out;
}

TEST(EscapeString, Eq) {
  EXPECT_EQ(escape_string_view(""sv), ""sv);
  EXPECT_EQ(escape_string_view("abc"sv), "abc"sv);
  EXPECT_EQ(escape_string_view("\0"sv), R"(\x00)"sv);
  EXPECT_EQ(escape_string_view("\n"sv), R"(\x0a)"sv);
  EXPECT_EQ(escape_string_view("\x1f !~\x7f"sv), R"(\x1f !~\x7f)"sv);
}

// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include <gtest/gtest.h>

struct DeferHelper {
  std::function<void()> dtor;
  DeferHelper(std::function<void()> ctor, std::function<void()> dtor)
      : dtor(dtor) {
    ctor();
  }
  ~DeferHelper() { dtor(); }
};

// Clear `val1` immediately
// and run func(val1, val2) when leaving the local scope.
#define CLEAR_AND_EVENTUALLY(func, val1, val2)                    \
  DeferHelper defer_clear_eventually_##val1([&]() { val1 = {}; }, \
                                            [&]() { func(val1, val2); })

#define EVENTUALLY_PRIVATE2(expr, name) \
  DeferHelper defer_eventually_##name([]() {}, [&]() { expr; })

#define EVENTUALLY_PRIVATE(expr, name) EVENTUALLY_PRIVATE2(expr, name)

// run func(val1, val2) when leaving the local scope
#define EVENTUALLY(func, val1, val2) \
  EVENTUALLY_PRIVATE(func(val1, val2), __LINE__)

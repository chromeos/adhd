// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_TESTS_TEST_UTIL_H_
#define CRAS_SRC_TESTS_TEST_UTIL_H_

#include <gtest/gtest.h>

template <typename T>
class DeferHelper {
 public:
  template <typename Ctor, typename Dtor>
  DeferHelper(Ctor ctor, Dtor dtor) : dtor_(dtor) {
    ctor();
  }
  ~DeferHelper() { dtor_(); }

 private:
  T dtor_;
};

template <typename Ctor, typename Dtor>
DeferHelper(Ctor, Dtor) -> DeferHelper<Dtor>;

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

#endif

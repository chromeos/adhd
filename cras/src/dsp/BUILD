# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cc_library(
    name = "eq2",
    srcs = ["eq2.c"],
    hdrs = ["eq2.h"],
    visibility = ["//src/benchmark:__pkg__"],
    deps = [":biquad"],
)

cc_library(
    name = "biquad",
    srcs = ["biquad.c"],
    hdrs = ["biquad.h"],
)

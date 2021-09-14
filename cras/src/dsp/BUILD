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
    name = "drc",
    srcs = ["drc.c"],
    hdrs = ["drc.h"],
    visibility = ["//src/benchmark:__pkg__"],
    deps = [
        ":crossover2",
        ":drc_kernel",
        ":eq2",
    ],
)

cc_library(
    name = "biquad",
    srcs = ["biquad.c"],
    hdrs = ["biquad.h"],
)

cc_library(
    name = "crossover2",
    srcs = ["crossover2.c"],
    hdrs = ["crossover2.h"],
    deps = [":biquad"],
)

cc_library(
    name = "drc_kernel",
    srcs = ["drc_kernel.c"],
    hdrs = ["drc_kernel.h"],
    deps = [":drc_math"],
)

cc_library(
    name = "drc_math",
    srcs = ["drc_math.c"],
    hdrs = ["drc_math.h"],
)

cc_library(
    name = "dsp_util",
    srcs = ["dsp_util.c"],
    hdrs = ["dsp_util.h"],
    copts = [
        "-Isrc/common",
    ],
    visibility = ["//src/benchmark:__pkg__"],
    deps = ["//src/common:cras_types"],
)

# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cc_library(
    name = "cras_mix_ops",
    srcs = [
        "cras_mix_ops.c",
        "cras_system_state.h",
    ],
    hdrs = ["cras_mix_ops.h"],
    copts = ["-Isrc/common"],
    visibility = ["//src/benchmark:__pkg__"],
    deps = ["//src/common:cras_types"],
)

# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cc_library(
    name = "cras_client",
    srcs = [
        "cras_client.c",
    ],
    hdrs = ["cras_client.h"],
    copts = [
        "-Isrc/common",
    ],
    linkopts = select({
        "//conditions:default": [
            "-lm",
            "-lrt",
        ],
    }),
    visibility = ["//src/benchmark:__pkg__"],
    deps = [
        "//src/common:cras_config",
        "//src/common:cras_file_wait",
        "//src/common:cras_messages",
        "//src/common:cras_observer_ops",
        "//src/common:cras_shm",
        "//src/common:cras_string",
        "//src/common:cras_types",
        "//src/common:cras_util",
        "//src/common:utlist",
    ],
)

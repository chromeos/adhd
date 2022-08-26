# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cc_library(
    name = "cras_config",
    srcs = [
        "cras_config.c",
    ],
    hdrs = [
        "cras_config.h",
    ],
    copts = [
        "-DCRAS_SOCKET_FILE_DIR=\\\"/var/run/cras\\\"",
    ],
    visibility = [
        "//src/libcras:__pkg__",
    ],
    deps = [
        "//src/common:cras_types",
    ],
)

cc_library(
    name = "cras_file_wait",
    srcs = [
        "cras_file_wait.c",
    ],
    hdrs = [
        "cras_file_wait.h",
    ],
    visibility = [
        "//src/libcras:__pkg__",
    ],
)

cc_library(
    name = "cras_messages",
    hdrs = [
        "cras_messages.h",
    ],
    visibility = [
        "//src/libcras:__pkg__",
    ],
)

cc_library(
    name = "cras_observer_ops",
    hdrs = [
        "cras_observer_ops.h",
    ],
    visibility = [
        "//src/libcras:__pkg__",
    ],
)

cc_library(
    name = "cras_shm",
    srcs = [
        "cras_shm.c",
    ],
    hdrs = [
        "cras_shm.h",
    ],
    visibility = [
        "//src/libcras:__pkg__",
    ],
    deps = [
        "//src/common:cras_string",
        "//src/common:cras_types",
        "//src/common:cras_util",
    ],
)

cc_library(
    name = "cras_string",
    srcs = [
        "cras_string.c",
        "strlcpy.c",
    ],
    hdrs = [
        "cras_string.h",
        "strlcpy.h",
    ],
    visibility = [
        "//src/common:__pkg__",
        "//src/libcras:__pkg__",
        "//src/server:__pkg__",
    ],
)

cc_library(
    name = "cras_types",
    srcs = [
        "cras_audio_format.c",
        "cras_iodev_info.h",
        "packet_status_logger.h",
    ],
    hdrs = [
        "cras_audio_format.h",
        "cras_types.h",
    ],
    visibility = [
        "//src/benchmark:__pkg__",
        "//src/dsp:__pkg__",
        "//src/libcras:__pkg__",
        "//src/server:__pkg__",
    ],
)

cc_library(
    name = "cras_util",
    srcs = ["cras_util.c"],
    hdrs = ["cras_util.h"],
    copts = [
        "-Isrc/common",
    ],
    visibility = [
        "//src/benchmark:__pkg__",
        "//src/libcras:__pkg__",
        "//src/server:__pkg__",
    ],
    deps = ["//src/common:cras_types"],
)

cc_library(
    name = "utlist",
    hdrs = ["utlist.h"],
    copts = [
        "-Isrc/common",
    ],
    visibility = [
        "//src/libcras:__pkg__",
        "//src/server:__pkg__",
    ],
)

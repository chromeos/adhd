# Copyright 2021 The ChromiumOS Authors
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

cc_library(
    name = "cras_alsa_helpers",
    srcs = [
        "cras_alsa_helpers.c",
    ],
    hdrs = ["cras_alsa_helpers.h"],
    copts = ["-Isrc/common"],
    visibility = ["//src/benchmark:__pkg__"],
    deps = [
        "//src/common:cras_types",
        "//src/common:cras_util",
    ],
)

cc_library(
    name = "cras_alsa_ucm",
    srcs = [
        "cras_alsa_mixer_name.c",
        "cras_alsa_mixer_name.h",
        "cras_alsa_ucm.c",
        "cras_alsa_ucm_section.c",
    ],
    hdrs = [
        "cras_alsa_ucm.h",
        "cras_alsa_ucm_section.h",
    ],
    copts = ["-Isrc/common"],
    visibility = ["//src/benchmark:__pkg__"],
    deps = [
        "//src/common:cras_string",
        "//src/common:cras_util",
        "//src/common:utlist",
    ],
)

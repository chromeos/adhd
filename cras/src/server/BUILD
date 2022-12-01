# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cc_library(
    name = "cras_mix",
    srcs = [
        "cras_mix.c",
        "cras_mix.h",
    ],
    local_defines = select({
        "//:x86_64_build": [
            "HAVE_SSE42",
            "HAVE_AVX",
            "HAVE_AVX2",
            "HAVE_FMA",
        ],
        "//conditions:default": [],
    }),
    visibility = ["//src/benchmark:__pkg__"],
    deps = ["cras_mix_ops"] + select({
        "//:x86_64_build": [
            ":cras_mix_ops_avx",
            ":cras_mix_ops_avx2",
            ":cras_mix_ops_fma",
            ":cras_mix_ops_sse42",
        ],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "cras_mix_ops",
    srcs = [
        "cras_mix_ops.c",
        "cras_system_state.h",
    ],
    hdrs = ["cras_mix_ops.h"],
    deps = ["//src/common:cras_types"],
)

cc_library(
    name = "cras_mix_ops_sse42",
    srcs = [
        "cras_mix_ops.c",
        "cras_system_state.h",
    ],
    hdrs = ["cras_mix_ops.h"],
    copts = [
        "-msse4.2",
        "-ffast-math",
    ],
    local_defines = ["OPS_SSE42"],
    target_compatible_with = ["@platforms//cpu:x86_64"],
    deps = ["//src/common:cras_types"],
)

cc_library(
    name = "cras_mix_ops_avx",
    srcs = [
        "cras_mix_ops.c",
        "cras_system_state.h",
    ],
    hdrs = ["cras_mix_ops.h"],
    copts = [
        "-mavx",
        "-ffast-math",
    ],
    local_defines = ["OPS_AVX"],
    target_compatible_with = ["@platforms//cpu:x86_64"],
    deps = ["//src/common:cras_types"],
)

cc_library(
    name = "cras_mix_ops_avx2",
    srcs = [
        "cras_mix_ops.c",
        "cras_system_state.h",
    ],
    hdrs = ["cras_mix_ops.h"],
    copts = [
        "-mavx2",
        "-ffast-math",
    ],
    local_defines = ["OPS_AVX2"],
    target_compatible_with = ["@platforms//cpu:x86_64"],
    deps = ["//src/common:cras_types"],
)

cc_library(
    name = "cras_mix_ops_fma",
    srcs = [
        "cras_mix_ops.c",
        "cras_system_state.h",
    ],
    hdrs = ["cras_mix_ops.h"],
    copts = [
        "-mavx2",
        "-mfma",
        "-ffast-math",
    ],
    local_defines = ["OPS_FMA"],
    target_compatible_with = ["@platforms//cpu:x86_64"],
    deps = ["//src/common:cras_types"],
)

cc_library(
    name = "cras_alsa_helpers",
    srcs = [
        "cras_alsa_helpers.c",
    ],
    hdrs = ["cras_alsa_helpers.h"],
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
    visibility = ["//src/benchmark:__pkg__"],
    deps = [
        "//src/common:cras_string",
        "//src/common:cras_util",
        "//src/common:utlist",
    ],
)

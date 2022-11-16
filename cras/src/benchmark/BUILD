# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cc_binary(
    name = "cras_bench",
    srcs = [
        "dsp_benchmark.cc",
        "mixer_ops_benchmark.cc",
    ] + select({
        "//:hw_dependent_build": ["alsa_benchmark.cc"],
        "//conditions:default": [],
    }) + select({
        "//:apm_build": ["apm_benchmark.cc"],
        "//conditions:default": [],
    }),
    copts = [
        "-Isrc/common",
        "-Isrc/libcras",
    ],
    linkopts = [
        "-lm",
    ] + select({
        "//:hw_dependent_build": [
            "-lasound",
            "-lcros_config",
        ],
        "//conditions:default": [
        ],
    }) + select({
        "//:apm_build": ["-lwebrtc_apm"],
        "//conditions:default": [],
    }),
    deps = [
        ":benchmark_util",
        "//src/dsp:drc",
        "//src/dsp:dsp_util",
        "//src/dsp:eq2",
        "//src/server:cras_mix_ops",
        "@com_github_google_benchmark//:benchmark_main",
    ] + select({
        "//:hw_dependent_build": [
            "//src/libcras:cras_client",
            "//src/server:cras_alsa_helpers",
            "//src/server:cras_alsa_ucm",
            "@pkg_config//:libcros_config",
            "@pkg_config//:libchrome",
        ],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "benchmark_util",
    srcs = ["benchmark_util.cc"],
    hdrs = ["benchmark_util.h"],
)

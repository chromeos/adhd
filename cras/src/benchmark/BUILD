# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@com_github_bazel_skylib//rules:common_settings.bzl", "string_flag")

string_flag(
    name = "apm",
    build_setting_default = "false",
)

config_setting(
    name = "apm_build",
    flag_values = {":apm": "true"},
)

cc_binary(
    name = "cras_bench",
    srcs = select({
        ":apm_build": [
            "apm_benchmark.cc",
            "dsp_benchmark.cc",
            "mixer_ops_benchmark.cc",
        ],
        "//conditions:default": [
            "dsp_benchmark.cc",
            "mixer_ops_benchmark.cc",
        ],
    }),
    copts = [
        "-Isrc/common",
    ],
    linkopts = select({
        ":apm_build": [
            "-lm",
            "-lwebrtc_apm",
        ],
        "//conditions:default": [
            "-lm",
        ],
    }),
    deps = [
        ":benchmark_util",
        "//src/dsp:drc",
        "//src/dsp:dsp_util",
        "//src/dsp:eq2",
        "//src/server:cras_mix_ops",
        "@com_github_google_benchmark//:benchmark_main",
    ],
)

cc_library(
    name = "benchmark_util",
    srcs = ["benchmark_util.cc"],
    hdrs = ["benchmark_util.h"],
)

# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@com_github_bazel_skylib//rules:common_settings.bzl", "string_flag")

string_flag(
    name = "hw_dependency",
    build_setting_default = "false",
)

config_setting(
    name = "hw_dependent_build",
    flag_values = {":hw_dependency": "true"},
)

cc_binary(
    name = "cras_bench",
    srcs = select({
        ":hw_dependent_build": [
            "apm_benchmark.cc",
            "dsp_benchmark.cc",
            "alsa_benchmark.cc",
            "mixer_ops_benchmark.cc",
        ],
        "//conditions:default": [
            "dsp_benchmark.cc",
            "mixer_ops_benchmark.cc",
        ],
    }),
    copts = [
        "-Isrc/common",
        "-Isrc/libcras",
    ],
    linkopts = select({
        ":hw_dependent_build": [
            "-lm",
            "-lwebrtc_apm",
            "-lasound",
            "-lcros_config",
        ],
        "//conditions:default": [
            "-lm",
        ],
    }),
    deps = select({
        ":hw_dependent_build": [
            ":benchmark_util",
            "//src/libcras:cras_client",
            "//src/dsp:drc",
            "//src/dsp:dsp_util",
            "//src/dsp:eq2",
            "//src/server:cras_mix_ops",
            "//src/server:cras_alsa_helpers",
            "//src/server:cras_alsa_ucm",
            "@com_github_google_benchmark//:benchmark_main",
            "@pkg_config//:libcros_config",
            "@pkg_config//:libchrome",
        ],
        "//conditions:default": [
            ":benchmark_util",
            "//src/dsp:drc",
            "//src/dsp:dsp_util",
            "//src/dsp:eq2",
            "//src/server:cras_mix_ops",
            "@com_github_google_benchmark//:benchmark_main",
        ],
    }),
)

cc_library(
    name = "benchmark_util",
    srcs = ["benchmark_util.cc"],
    hdrs = ["benchmark_util.h"],
)

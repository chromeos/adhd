# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cc_binary(
    name = "cras_bench",
    deps = [
        ":benchmark_util",
        ":default_benchmarks",
        "@com_github_google_benchmark//:benchmark_main",
    ] + select({
        "//:apm_build": [":apm_benchmark"],
        "//conditions:default": [],
    }) + select({
        "//:hw_dependent_build": ["alsa_benchmark"],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "default_benchmarks",
    srcs = [
        "dsp_benchmark.cc",
        "mixer_ops_benchmark.cc",
    ],
    deps = [
        ":benchmark_util",
        "//src/dsp:drc",
        "//src/dsp:dsp_util",
        "//src/dsp:eq2",
        "//src/server:cras_mix_ops",
        "@com_github_google_benchmark//:benchmark",
    ],
)

cc_library(
    name = "apm_benchmark",
    srcs = [
        "apm_benchmark.cc",
    ],
    linkopts = [
        "-lwebrtc_apm",
    ],
    deps = [
        ":benchmark_util",
        "//src/dsp:dsp_util",
        "//src/server:cras_mix_ops",
        "@com_github_google_benchmark//:benchmark",
    ],
)

cc_library(
    name = "alsa_benchmark",
    srcs = [
        "alsa_benchmark.cc",
    ],
    linkopts = [
        "-lasound",
        "-lcros_config",
    ],
    deps = [
        ":benchmark_util",
        "//src/libcras:cras_client",
        "//src/server:cras_alsa_helpers",
        "//src/server:cras_alsa_ucm",
        "//src/server:cras_mix_ops",
        "@com_github_google_benchmark//:benchmark",
        "@pkg_config//:libchrome",
        "@pkg_config//:libcros_config",
    ],
)

cc_library(
    name = "benchmark_util",
    srcs = ["benchmark_util.cc"],
    hdrs = ["benchmark_util.h"],
)

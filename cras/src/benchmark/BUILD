# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cc_binary(
    name = "cras_bench",
    srcs = [
        "dsp_benchmark.cc",
        "mixer_ops_benchmark.cc",
    ],
    copts = [
        "-Isrc/common",
    ],
    linkopts = ["-lm"],
    deps = [
        "//src/dsp:eq2",
        "//src/server:cras_mix_ops",
        "@com_github_google_benchmark//:benchmark_main",
    ],
)

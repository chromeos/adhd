# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
load("@com_github_bazel_skylib//rules:common_settings.bzl", "bool_flag")

bool_flag(
    name = "apm",
    build_setting_default = False,
)

config_setting(
    name = "apm_build",
    flag_values = {":apm": "true"},
    visibility = [":__subpackages__"],
)

bool_flag(
    name = "hw_dependency",
    build_setting_default = False,
)

config_setting(
    name = "hw_dependent_build",
    flag_values = {":hw_dependency": "true"},
    visibility = [":__subpackages__"],
)

bool_flag(
    name = "ml",
    build_setting_default = False,
)

config_setting(
    name = "ml_build",
    flag_values = {":ml": "true"},
    visibility = [":__subpackages__"],
)

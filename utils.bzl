# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

def bool_flag_config(name, build_setting_default):
    """Creates a bool_flag and a config_setting associated with it.

    The bool_flag is named ${name}.
    The config_setting is named ${name}_build.
    """
    bool_flag(
        name = name,
        build_setting_default = build_setting_default,
    )

    native.config_setting(
        name = "{}_build".format(name),
        flag_values = {":{}".format(name): "true"},
    )

def define_feature(condition, define):
    """Defines `define`=1 if condition is true, otherwise 0"""
    return select({
        condition: ["{}=1".format(define)],
        "//conditions:default": ["{}=0".format(define)],
    })

def require_config(label):
    """Returns a select() to require a config_setting for a target."""
    return select({
        label: [],
        "//conditions:default": ["@platforms//:incompatible"],
    })

def require_no_config(label):
    """Returns a select() to require a config_setting to be not set for a target."""
    return select({
        label: ["@platforms//:incompatible"],
        "//conditions:default": [],
    })

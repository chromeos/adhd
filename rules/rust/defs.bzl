# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@rules_rust//rust:defs.bzl", "rust_library")
load("//:utils.bzl", "require_no_config")

def cras_rust_library(name, crate_name = None, visibility = None, **kwargs):
    """
    Wraps a cras_rust library.

    Selects the rust_library built by Bazel or libcras_rust.a provided by
    the system depending on the build configuration.

    Args:
        name: The name of the alias
        crate_name: Passed to rust_library
        visibility: visibility of the alias
        **kwargs: Passed to rust_library
    """

    if crate_name == None:
        crate_name = name

    native.alias(
        name = name,
        actual = select({
            "//:system_cras_rust_build": "//cras/src/server/rust/system_cras_rust",
            "//conditions:default": ":{}__rust_library".format(name),
        }),
        visibility = visibility,
    )

    rust_library(
        name = "{}__rust_library".format(name),
        crate_name = crate_name,
        target_compatible_with = require_no_config("//:system_cras_rust_build"),
        **kwargs
    )

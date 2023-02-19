# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def rust_library(name, **_kwargs):
    return native.cc_library(
        name = name,
        target_compatible_with = ["@platforms//:incompatible"],
    )

rust_test = rust_library
rust_binary = rust_library
rust_test_suite = rust_library

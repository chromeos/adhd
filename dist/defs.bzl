# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes")

EXECUTABLE_ATTRIBUTES = pkg_attributes(
    group = "root",
    mode = "0755",
    user = "root",
)

FILE_ATTRIBUTES = pkg_attributes(
    group = "root",
    mode = "0644",
    user = "root",
)

LIBDIR = select({
    ":lib_is_lib": "usr/lib",
    ":lib_is_lib32": "usr/lib32",
    ":lib_is_lib64": "usr/lib64",
})

LOCAL_LIBDIR = select({
    ":lib_is_lib": "usr/local/lib",
    ":lib_is_lib32": "usr/local/lib32",
    ":lib_is_lib64": "usr/local/lib64",
})

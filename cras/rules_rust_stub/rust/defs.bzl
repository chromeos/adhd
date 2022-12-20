# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def _noop_impl(_ctx):
    pass

_attrs = {
    "deps": attr.label_list(),
    "srcs": attr.label_list(allow_files = [".rs"]),
    "crate": attr.label(),
}

rust_test = rule(
    implementation = _noop_impl,
    attrs = _attrs,
    test = True,
)

rust_library = rule(
    implementation = _noop_impl,
    attrs = _attrs,
)

rust_static_library = rust_library

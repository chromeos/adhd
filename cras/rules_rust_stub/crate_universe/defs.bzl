# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def _crate_spec(**_kwargs):
    return ""

crate = struct(
    spec = _crate_spec,
)

def _crates_repository_impl(repository_ctx):
    build_file_contents = """package(default_visibility = ["//visibility:public"])
"""
    for name in repository_ctx.attr.packages:
        build_file_contents += """cc_library(name = "{}")
""".format(name)
    repository_ctx.file("WORKSPACE", "")
    repository_ctx.file("BUILD", build_file_contents)
    repository_ctx.file("defs.bzl", """def crate_repositories():
    pass

def all_crate_deps(**_kwargs):
    return []
""")

crates_repository = repository_rule(
    implementation = _crates_repository_impl,
    attrs = dict(
        cargo_lockfile = attr.string(),
        lockfile = attr.string(),
        packages = attr.string_dict(),
        manifests = attr.string_list(),
    ),
)

# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A repository rule to list http_archive dependencies and store them in a file.

This help us maintail the bazel_external_uris in our ebuild:
https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/chromiumos-overlay/media-sound/adhd/adhd-9999.ebuild
"""

def _deps_json():
    deps = {}

    for name, rule in native.existing_rules().items():
        if rule["kind"] not in ("http_archive", "http_file"):
            continue

        # Ignore rules_rust generated repisotiries
        if rule.get("generator_function") in (
            "crate_universe_dependencies",
            "rules_rust_dependencies",
            "crate_repositories",
        ):
            continue

        deps[name] = dict(rule)

    return json.encode_indent(deps)

def _impl(repository_ctx):
    repository_ctx.file("deps.json", repository_ctx.attr.deps_json)
    repository_ctx.file("bazel_external_uris_exclude.json", json.encode_indent(repository_ctx.attr.bazel_external_uris_exclude))
    repository_ctx.template("BUILD.bazel", Label("//repositories/http_archive_deps:BUILD.deps_json.bzl"))

_http_archive_deps_repository = repository_rule(
    implementation = _impl,
    attrs = {
        "deps_json": attr.string(),
        "bazel_external_uris_exclude": attr.string_list(
            doc = "List of http_archive()s to exclude from bazel_external_uris",
        ),
    },
)

def http_archive_deps_setup(bazel_external_uris_exclude):
    _http_archive_deps_repository(
        name = "deps_json",
        deps_json = _deps_json(),
        bazel_external_uris_exclude = bazel_external_uris_exclude,
    )

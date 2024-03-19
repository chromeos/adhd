# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def _system_cras_rust_repository_impl(repository_ctx):
    lib_path = repository_ctx.os.environ["SYSTEM_CRAS_RUST_LIB"]

    # Read it first so it's easier to tell if the lib does not exist.
    repository_ctx.read(lib_path)

    repository_ctx.symlink(lib_path, "libcras_rust.a")

    repository_ctx.template("BUILD.bazel", Label(":BUILD.system_cras_rust.bazel"))
    repository_ctx.file(
        "WORKSPACE",
        """workspace(name = "{name}")
""".format(name = repository_ctx.name),
    )

system_cras_rust_repository = repository_rule(
    implementation = _system_cras_rust_repository_impl,
    environ = ["SYSTEM_CRAS_RUST_LIB"],
    doc = """Creates a cc_import target for the system cras_rust library.""",
)

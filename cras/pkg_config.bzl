# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Provide repository rules for pkg-config libraries."""

load("@com_github_bazel_skylib//lib:paths.bzl", "paths")

_PKG_CONFIG_LIBRARY = """
cc_library(
    name = "{name}",
    hdrs = glob([
{hdrs_spec}
    ]),
    defines = [
{defines}
    ],
    includes = [
{includes}
    ],
    linkopts = [
{linkopts}
    ],
)
"""

def _pkg_config_library_entry(name, hdrs_globs, defines, includes, linkopts):
    return _PKG_CONFIG_LIBRARY.format(
        name = name,
        hdrs_spec = "\n".join(["        \"{}\",".format(g) for g in hdrs_globs]),
        defines = "\n".join(["        \"{}\",".format(d) for d in defines]),
        includes = "\n".join(["        \"{}\",".format(d) for d in includes]),
        linkopts = "\n".join(["        \"{}\",".format(opt) for opt in linkopts]),
    )

def _maybe_fixup_lib_for_oss_fuzz(linkopt, oss_fuzz_static):
    """Try to use static libs for oss-fuzz."""
    if not oss_fuzz_static:
        return linkopt
    if not linkopt.startswith("-l"):
        return linkopt
    if linkopt == "-ludev":
        # static udev is not available
        return linkopt
    return "-l:lib{}.a".format(linkopt[2:])

def _pkg_config(repository_ctx, library):
    pkg_config = repository_ctx.os.environ.get("PKG_CONFIG", default = "pkg-config").split(" ")
    cmd = pkg_config + ["--cflags", "--libs", library]
    result = repository_ctx.execute(
        cmd,
    )
    if result.return_code != 0:
        fail("""{cmd} failure (code {return_code});
{stderr}""".format(cmd = " ".join([str(s) for s in cmd]), return_code = result.return_code, stderr = result.stderr))

    oss_fuzz_static = repository_ctx.os.environ.get("OSS_FUZZ_STATIC_PKG_CONFIG_DEPS")

    defines = []
    includes = []
    linkopts = []
    for flag in result.stdout.strip().split(" "):
        if flag.startswith("-D"):
            defines.append(flag[2:])
        elif flag.startswith("-I"):
            includes.append(flag[2:])
        else:
            linkopts.append(_maybe_fixup_lib_for_oss_fuzz(flag, oss_fuzz_static))

    return struct(
        defines = defines,
        includes = includes,
        linkopts = linkopts,
    )

def _pkg_config_library(repository_ctx, library, library_root, defines = []):
    result = _pkg_config(repository_ctx, library)

    # Replace include directories to symlinked directories under library_root.
    # library_root is symlinked to "/" so system libs can be included.
    includes = [
        library_root + path
        for path in result.includes
    ]
    hdrs_globs = [
        # Bazel rejects .. in glob patterns so we normalize them.
        # Note that normalize() is logical, so if /a/b/c is a symlink
        # to another directory then /a/b/c/.. would resolve incorrectly.
        "{}{}/**/*.h".format(library_root, paths.normalize(path))
        for path in result.includes
    ]

    return _pkg_config_library_entry(
        name = library,
        hdrs_globs = hdrs_globs,
        defines = defines + result.defines,
        includes = includes,
        linkopts = result.linkopts,
    )

_pkg_config_repository_attrs = {
    "libs": attr.string_list(
        doc = """The names of the libraries to include (as passed to pkg-config)""",
    ),
    "additional_build_file_contents": attr.string(
        default = "",
        mandatory = False,
        doc = """Additional content to inject into the build file.""",
    ),
}

def _pkg_config_repository(repository_ctx, libs, additional_build_file_contents):
    # Create BUILD with the cc_library section for each library.
    build_file_contents = """package(default_visibility = ["//visibility:public"])
"""

    library_root = "library_root"
    repository_ctx.symlink("/", library_root)

    for library in libs:
        build_file_contents += _pkg_config_library(repository_ctx, library, library_root)

    build_file_contents += additional_build_file_contents

    repository_ctx.file(
        "WORKSPACE",
        """workspace(name = "{name}")
""".format(name = repository_ctx.name),
    )
    repository_ctx.file("BUILD", build_file_contents)

def _pkg_config_repository_impl(repository_ctx):
    """Implementation of the pkg_config_repository rule."""
    return _pkg_config_repository(
        repository_ctx,
        libs = repository_ctx.attr.libs,
        additional_build_file_contents = repository_ctx.attr.additional_build_file_contents,
    )

pkg_config_repository = repository_rule(
    implementation = _pkg_config_repository_impl,
    attrs = _pkg_config_repository_attrs,
    environ = ["PKG_CONFIG", "OSS_FUZZ_STATIC_PKG_CONFIG_DEPS"],
    doc =
        """Makes pkg-config-enabled libraries available for binding.

If the environment variable PKG_CONFIG is set, this rule will use its value
as the `pkg-config` command.

Examples:
  Suppose the current repository contains the source code for a chat program,
  rooted at the directory `~/chat-app`. It needs to depend on an SSL library
  which is available from the current system, registered with pkg-config.

  Targets in the `~/chat-app` repository can depend on this library through the
  target @system_libs//:openssl if the following lines are added to
  `~/chat-app/WORKSPACE`:
  ```python
  load(":system_libs.bzl", "pkg_config_repository")
  pkg_config_repository(
      name = "system_libs",
      libs = ["openssl"],
  )
  ```
  Then targets would specify `@system_libs//:openssl` as a dependency.
""",
)

def _expand_var_recursively(ctx, var, default):
    return ctx.expand_make_variables("_internal_" + var, ctx.var.get(var, default), {})

def _generate_pc_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name)

    # These variables are defined in bazel.eclass:
    # https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/portage-stable/eclass/bazel.eclass;l=119;drc=80067222e804d5fcf95a75956b6953ac02520278
    prefix = _expand_var_recursively(ctx, "PREFIX", "/usr/local")
    libdir = _expand_var_recursively(ctx, "LIBDIR", prefix + "/lib")
    includedir = _expand_var_recursively(ctx, "INCLUDEDIR", prefix + "/include")

    ctx.actions.expand_template(
        output = out,
        template = ctx.file.src,
        substitutions = {
            "@prefix@": prefix,
            "@exec_prefix@": prefix,
            "@libdir@": libdir,
            "@includedir@": includedir,
            "@PACKAGE_VERSION@": "0.1",
        },
    )
    return [DefaultInfo(files = depset([out]))]

generate_pc = rule(
    implementation = _generate_pc_impl,
    attrs = dict(
        src = attr.label(
            allow_single_file = [".pc.in"],
            mandatory = True,
        ),
    ),
    doc = """Generate a pkg-config file (.pc) from a template (.pc.in)""",
)

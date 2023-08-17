# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Provide repository rules for pkg-config libraries."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@bazel_skylib//lib:unittest.bzl", "asserts", "unittest")

_PKG_CONFIG_LIBRARY = """\
cc_library(
    name = {name},
    hdrs = glob({hdrs_spec}),
    defines = {defines},
    includes = {includes},
    linkopts = {linkopts},
    visibility = {visibility},
)
"""

def _pkg_config_library_entry(name, hdrs_globs, defines, includes, linkopts, visibility):
    return _PKG_CONFIG_LIBRARY.format(
        name = repr(name),
        hdrs_spec = repr(hdrs_globs),
        defines = repr(defines),
        includes = repr(includes),
        linkopts = repr(linkopts),
        visibility = repr(visibility),
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

def _indent(text, indent):
    return indent + text.replace("\n", "\n" + indent)

def _pkg_config(repository_ctx, library):
    pkg_config = repository_ctx.os.environ.get("PKG_CONFIG", default = "pkg-config").split(" ")
    cmd = pkg_config + ["--cflags", "--libs", library]
    result = repository_ctx.execute(
        cmd,
    )
    if result.return_code != 0:
        fail("""
Package {library} unavailable: command {cmd} failed with code {return_code}:
{stderr}
""".format(
            cmd = repr(" ".join([str(s) for s in cmd])),
            return_code = result.return_code,
            stderr = _indent(result.stderr.strip("\n"), "> "),
            library = repr(library),
        ))

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

def _common_roots(paths):
    """
    Return common roots for paths.

    Paths must be absolute and normalized.

    Example:
    _common_roots(["/a/b", "/a/b/c", "/a/d", "/a/e"]) == ["/a/b", "/a/d", "/a/e"]
    """
    prev_path = None
    roots = []
    for path in sorted(paths):
        if path[0] != "/":
            fail("{} is not absolute".format(repr(path)))

        # Strip trailing "/", except for root (/).
        path = path[0] + path[1:].rstrip("/")

        if prev_path != None and (path.rstrip("/") + "/").startswith(prev_path.rstrip("/") + "/"):
            # path is a descendant of prev_path
            continue

        prev_path = path
        roots.append(path)
    return roots

def _symlink_includes(repository_ctx, prefix, includes):
    """
    Symlink system includes to a local directory and return the localized include paths.

    For each child directory in the pkg_config include path, create a symlink under the external/ folder and add the
    correct -isystem path.
    Example:
        symlink src: /build/hatch/usr/include/dbus-1.0
        symlink dst: $(bazel info output_base)/external/pkg_config__dbus-1/${prefix}/usr/include
/dbus-1.0
    """

    # Clean up .. in path segments.
    # Note that paths.normalize() is logical, so if /a/b/c is a symlink
    # to another directory, then /a/b/c/.. would resolve incorrectly.
    includes = [paths.normalize(inc) for inc in includes]

    local_includes = []
    for inc in _common_roots(includes):
        repository_ctx.symlink(inc, prefix + inc)
        local_includes.append(prefix + inc)

    return local_includes

def _pkg_config_library_impl(repository_ctx):
    library = repository_ctx.attr.library

    result = _pkg_config(repository_ctx, library)
    includes = _symlink_includes(repository_ctx, "sysroot", result.includes)
    hdrs_globs = [
        "{}/**/*.h".format(inc)
        for inc in includes
    ]
    if library == "libcros_config":
        # Hack for b/296440405.
        # The sysroot may change during src_compile() and Bazel is sensitive to changes.
        # We only actually need $SYSROOT/usr/include/chromeos/chromeos-config, but
        # pkg-config would return -I$SYSROOT/usr/include/chromeos.
        # glob on only the chromeos-config directory instead, so Bazel doesn't
        # see changes out of chromeos-config/.
        # TODO: Remove hack once we are with Alchemy, which gives us immutable sysroots.
        hdrs_globs = [
            glob.replace("/usr/include/chromeos/**/*.h", "/usr/include/chromeos/chromeos-config/**/*.h")
            for glob in hdrs_globs
        ]

    build_file_contents = _pkg_config_library_entry(
        name = repository_ctx.name,
        hdrs_globs = hdrs_globs,
        defines = result.defines,
        includes = includes,
        linkopts = result.linkopts,
        visibility = repository_ctx.attr.library_visibility,
    )

    repository_ctx.file("BUILD.bazel", build_file_contents)
    repository_ctx.file(
        "WORKSPACE",
        """workspace(name = "{name}")
""".format(name = repository_ctx.name),
    )

_pkg_config_library = repository_rule(
    implementation = _pkg_config_library_impl,
    attrs = {
        "library": attr.string(
            doc = "The name of the library (as passed to pkg-config)",
        ),
        "library_visibility": attr.string_list(
            doc = "Visibility of the generated rules",
        ),
    },
    environ = ["PKG_CONFIG", "OSS_FUZZ_STATIC_PKG_CONFIG_DEPS"],
    local = True,
    configure = True,
    doc = """Makes a pkg-config-enabled library available for binding.

If the environment variable PKG_CONFIG is set, this rule will use its value
as the `pkg-config` command.

If OSS_FUZZ_STATIC_PKG_CONFIG_DEPS is set, this rule will prefer static
libraries. This environment variable is intended for oss-fuzz, where the
runtime image does not have the dynamic system dependencies.
""",
)

def _pkg_config_aggregate_impl(repository_ctx):
    for library in repository_ctx.attr.libs:
        repository_ctx.file(
            "{library}/BUILD.bazel".format(library = library),
            """\
alias(
    name = {name},
    actual = {actual},
    visibility = ["//visibility:public"],
)
""".format(
                name = repr(library),
                actual = repr("@{name}__{library}".format(name = repository_ctx.name, library = library)),
            ),
        )

    repository_ctx.file(
        "WORKSPACE",
        """workspace(name = "{name}")
""".format(name = repository_ctx.name),
    )

_pkg_config_aggregate = repository_rule(
    implementation = _pkg_config_aggregate_impl,
    attrs = {
        "libs": attr.string_list(
            doc = "The name of the libraries (as passed to pkg-config)",
        ),
    },
    doc = "Collects pkg_config_library rules",
)

def pkg_config(name, libs):
    _pkg_config_aggregate(name = name, libs = libs)
    for library in libs:
        _pkg_config_library(
            name = "{}__{}".format(name, library),
            library = library,
            library_visibility = ["@{}//:__subpackages__".format(name)],
        )

def _common_roots_test_impl(ctx):
    env = unittest.begin(ctx)

    asserts.equals(
        env,
        ["/a/b", "/a/d", "/a/e"],
        _common_roots(["/a/b", "/a/b/c", "/a/d", "/a/e"]),
    )

    asserts.equals(
        env,
        ["/"],
        _common_roots(["/"]),
    )

    asserts.equals(
        env,
        ["/"],
        _common_roots(["/b", "/a", "/"]),
    )

    asserts.equals(
        env,
        ["/"],
        _common_roots(["/", "/a", "/b"]),
    )

    asserts.equals(
        env,
        ["/a"],
        _common_roots(["/a", "/a/b", "/a/b/c"]),
    )

    asserts.equals(
        env,
        ["/a"],
        _common_roots(["/a/b", "/a", "/a/b/c"]),
    )

    asserts.equals(
        env,
        ["/a", "/b"],
        _common_roots(["/a", "/b/c", "/a/d", "/b"]),
    )

    asserts.equals(
        env,
        ["/a", "/b"],
        _common_roots(["/a", "/b", "/b", "/a"]),
    )

    asserts.equals(
        env,
        ["/a", "/ab"],
        _common_roots(["/a", "/ab", "/a/b"]),
    )

    asserts.equals(
        env,
        ["/a/b", "/a/c"],
        _common_roots(["/a/c", "/a/b"]),
    )

    asserts.equals(
        env,
        ["/a/b"],
        _common_roots(["/a/b/"]),
    )

    asserts.equals(
        env,
        ["/a/b"],
        _common_roots(["/a/b/", "/a/b"]),
    )

    return unittest.end(env)

common_roots_test = unittest.make(_common_roots_test_impl)

def pkg_config_test_suite():
    unittest.suite(
        "pkg_config_test_suite",
        common_roots_test,
    )

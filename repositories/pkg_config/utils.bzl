# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

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

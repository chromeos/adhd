# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Figure out the default linker flags in the current cc toolchain config."""

load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "CPP_LINK_EXECUTABLE_ACTION_NAME")
load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")

def _default_link_flags_impl(ctx):
    # Based on http://shortn/_zaPDnSBLyy
    cc_toolchain = find_cpp_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    variables = cc_common.create_link_variables(
        cc_toolchain = cc_toolchain,
        feature_configuration = feature_configuration,
        user_link_flags = ctx.fragments.cpp.linkopts,
    )
    compiler_options = cc_common.get_memory_inefficient_command_line(
        feature_configuration = feature_configuration,
        action_name = CPP_LINK_EXECUTABLE_ACTION_NAME,
        variables = variables,
    )

    compiler_options = [opt for opt in compiler_options if not opt.startswith("-l")]
    output = ctx.actions.declare_file(ctx.attr.name)
    ctx.actions.write(output, " ".join(compiler_options))
    return [DefaultInfo(files = depset([output]))]

default_link_flags = rule(
    implementation = _default_link_flags_impl,
    attrs = {
        "_cc_toolchain": attr.label(
            default = Label("@bazel_tools//tools/cpp:current_cc_toolchain"),
        ),
    },
    fragments = ["cpp"],
    toolchains = use_cpp_toolchain(),
)

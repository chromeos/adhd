# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@aspect_bazel_lib//lib:write_source_files.bzl", "write_source_file")
load("@bazel_skylib//lib:paths.bzl", "paths")
load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load("@rules_rust//rust:rust_common.bzl", "CrateInfo")
load("//:utils.bzl", "require_no_config")

def _do_cras_cbindgen_impl(ctx):
    args = ctx.actions.args()

    crate_info = ctx.attr.lib[CrateInfo]

    args.add(ctx.outputs.out, format = "--output=%s")
    args.add(ctx.file.assume_output.short_path, format = "--assume-output=%s")
    args.add(crate_info.root, format = "--with-src=%s")
    args.add_all(ctx.attr.extra_args)
    args.add(ctx.attr.copyright_year, format = "--copyright-year=%s")

    ctx.actions.run(
        executable = ctx.executable._cbindgen,
        inputs = crate_info.srcs,
        arguments = [args],
        outputs = [ctx.outputs.out],
        mnemonic = "CrasCbindgen",
        env = {
            "CRAS_CBINDGEN_LOG": ctx.attr._log_level[BuildSettingInfo].value,
        },
    )

do_cras_cbindgen = rule(
    implementation = _do_cras_cbindgen_impl,
    attrs = dict(
        lib = attr.label(providers = [CrateInfo]),
        out = attr.output(mandatory = True),
        assume_output = attr.label(allow_single_file = True),
        extra_args = attr.string_list(),
        copyright_year = attr.int(mandatory = True),
        _cbindgen = attr.label(
            executable = True,
            cfg = "exec",
            default = Label(":cbindgen"),
        ),
        _log_level = attr.label(
            default = Label(":log_level"),
        ),
    ),
)

def cras_cbindgen(name, lib, out, copyright_year, extra_args = []):
    if ":" in out:
        fail("out = {} should not contain a colon {}".format(repr(out), repr(":")))

    generated_out = paths.join(paths.dirname(out), "generated_" + paths.basename(out))

    do_cras_cbindgen(
        name = "do_generate_{}".format(name),
        lib = lib,
        out = generated_out,
        assume_output = out,
        copyright_year = copyright_year,
        extra_args = extra_args,
        target_compatible_with = require_no_config("//:system_cras_rust_build"),
    )

    write_source_file(
        name = "generate_{}".format(name),
        in_file = generated_out,
        out_file = out,
        target_compatible_with = require_no_config("//:system_cras_rust_build"),
        visibility = ["//build/write_source_files:__pkg__"],
    )

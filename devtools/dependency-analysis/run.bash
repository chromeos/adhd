#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

bazel_opts=(
    # Ensure intermediate artifacts are available.
    "--remote_download_outputs=all"
)

cd "$(dirname "${BASH_SOURCE[0]}")"
here="${PWD}"

pushd ../../ > /dev/null # adhd
    bazel build "${bazel_opts[@]}" //...
    bazel aquery "${bazel_opts[@]}" 'mnemonic(CppCompile, //...)' --output=proto --include_commandline=false > "${here}/actions.binpb"
popd > /dev/null

go run ./ -actions-proto=actions.binpb -adhd-dir=../..

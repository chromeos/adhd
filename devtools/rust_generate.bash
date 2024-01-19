#!/bin/bash

# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script re-generates rust-related files.

set -eux

ADHD="$(realpath "$(dirname "${BASH_SOURCE[0]}")/..")"

cd "${ADHD}"
CARGO_BAZEL_REPIN=true bazel sync --only=crate_index

for target in $(bazel query 'kind(write_source_file, //...)')
do
    bazel run "${target}"
done

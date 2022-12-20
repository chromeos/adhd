#!/bin/bash
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -o pipefail
cd "$(dirname -- "${BASH_SOURCE[0]}")"
go run .| buildifier -type build > ../cras/src/tests/BUILD.bazel

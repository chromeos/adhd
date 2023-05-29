#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -ex
cd "$(dirname ${BASH_SOURCE[0]})/quick-verifier"
go run ./cmd/copgen ../../.cop/build.yaml "$@"

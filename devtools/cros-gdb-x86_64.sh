#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
# execute gdb within chroot
cros_sdk /usr/bin/x86_64-cros-linux-gnu-gdb "$@"

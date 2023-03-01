#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
CHROMIUM_OS_PATH="/usr/local/google/home/$USER/chromiumos"
# create soft link for GDB find the source file in the chroot
sudo ln -sf "$CHROMIUM_OS_PATH" "$CHROMIUM_OS_PATH/chroot/$CHROMIUM_OS_PATH"
# execute gdb within chroot
sudo chroot "$CHROMIUM_OS_PATH/chroot" /usr/bin/x86_64-cros-linux-gnu-gdb $@

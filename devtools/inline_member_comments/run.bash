#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

bazel run //inline_member_comments -- \
    --extra-arg=-I/usr/lib/gcc/x86_64-linux-gnu/12/include \
    --extra-arg=-fparse-all-comments \
    "$@"

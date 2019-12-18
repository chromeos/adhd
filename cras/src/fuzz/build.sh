#!/bin/bash -eux

# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Builds fuzzers from within a container into /out/ directory.
# Expects /src/adhd to contain an adhd checkout.

cd $SRC/adhd/cras
./git_prepare.sh

FUZZER_LDFLAGS="${FUZZER_LDFLAGS} ${LIB_FUZZING_ENGINE}"
./configure --disable-dbus --enable-fuzzer

# Compile fuzzer
make -j$(nproc)

# Copy fuzzer to /out/ directory
cp $SRC/adhd/cras/src/cras_rclient_message_fuzzer $OUT/rclient_message

#!/bin/bash -eux
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Builds fuzzers from within a container into /out/ directory.
# Expects /src/cras to contain a cras checkout.

mkdir $WORK/cras
cd $SRC/cras
./git_prepare.sh
./configure --disable-dbus --disable-webrtc-apm
cargo build --release --manifest-path $SRC/cras/src/server/rust/Cargo.toml && \
  cp $SRC/cras/src/server/rust/target/release/libcras_rust.a /usr/local/lib
make -j$(nproc)

$CXX $CXXFLAGS $FUZZER_LDFLAGS \
  $SRC/cras/src/fuzz/rclient_message.cc -o $OUT/rclient_message \
  -I $SRC/cras/src/server \
  -I $SRC/cras/src/common \
  $SRC/cras/src/.libs/libcrasserver.a \
  -lcras_rust -lpthread -lrt -ludev -ldl -lm \
  -lFuzzingEngine \
  -Wl,-Bstatic -liniparser -lasound -lspeexdsp -Wl,-Bdynamic

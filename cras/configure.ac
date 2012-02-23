# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
AC_INIT([cras], [0.1], [dgreid@chromium.org],
             [cras], [http://www.chromium.org/])
AC_PREREQ([2.59])
AM_INIT_AUTOMAKE([1.10 -Wall no-define])
#AC_CONFIG_HEADERS([config.h])
AC_PROG_LIBTOOL
AC_PROG_CC
# c++ unit test (gtest).
AC_PROG_CXX
AC_LANG_C
AM_PROG_CC_C_O
PKG_PROG_PKG_CONFIG
AC_CONFIG_FILES([Makefile src/Makefile libcras.pc])
AC_OUTPUT

PKG_CHECK_MODULES([ASOUNDLIB], [ alsa >= 1.0.23 ])
PKG_CHECK_MODULES([LIBSPEEX], [ speexdsp >= 1.2 ])

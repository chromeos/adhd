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
#AC_CONFIG_FILES([Makefile src/Makefile libcras.pc])

PKG_CHECK_MODULES([LIBSPEEX], [ speexdsp >= 1.2 ])
PKG_CHECK_MODULES([ASOUNDLIB], [ alsa >= 1.0.23 ])
PKG_CHECK_MODULES([DBUS], [ dbus-1 >= 1.4.12 ])
AC_CHECK_LIB(asound, snd_pcm_ioplug_create,,
	     AC_ERROR([*** libasound has no external plugin SDK]), -ldl)

AC_ARG_ENABLE([alsa-plugin], AS_HELP_STRING([--disable-alsa-plugin],
                                            [Disable building of ALSA plugin]))

AC_ARG_ENABLE(chromeos-metrics,
	AC_HELP_STRING([--enable-chromeos-metrics], [Use ChromeOS metrics lib]),
	[if test "${enable_chromeos_metrics}" = "yes" ; then
		AC_DEFINE(_WITH_CHROMEOS_METRICS)
		METRICS_LIB="-lmetrics"
		AC_SUBST(METRICS_LIB)
	fi],
	)

# Determine ALSA plugin directory.
test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix=$prefix

AC_ARG_WITH(plugindir,
    AS_HELP_STRING([--with-plugindir=dir],
	[path where ALSA plugin files are stored]),
    plugindir="$withval", plugindir="")
if test -z "$plugindir"; then
    eval dir="$libdir"
    case "$dir" in
    /*) ;;
    *) dir="$dir"
    esac
    plugindir="$dir/alsa-lib"
fi
AC_DEFINE_UNQUOTED(ALSA_PLUGIN_DIR, "$plugindir",
                   [directory containing ALSA add-on modules])
ALSA_PLUGIN_DIR="$plugindir"
AC_SUBST(ALSA_PLUGIN_DIR)

# Get iniparser library and include locations
AC_ARG_WITH([iniparser-include-path],
  [AS_HELP_STRING([--with-iniparser-include-path],
      [location of the iniparser headers, defaults to /usr/include/])],
        [INIPARSER_CFLAGS="-I$withval"],
	  [INIPARSER_CFLAGS='-I/usr/include'])
AC_SUBST([INIPARSER_CFLAGS])

AC_ARG_WITH([iniparser-lib-path],
  [AS_HELP_STRING([--with-iniparser-lib-path],
      [location of the iniparser libraries])],
        [INIPARSER_LIBS="-L$withval -liniparser"],
          [INIPARSER_LIBS='-liniparser'])
AC_SUBST([INIPARSER_LIBS])

AC_OUTPUT([
	Makefile
	src/Makefile
	libcras.pc
])

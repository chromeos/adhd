# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file contains definitions which control the C compiler.


COPTIONS =					\
	-O3					\
	-funit-at-a-time

# Compiler is too old to support
#
#	-Wframe-larger-than=256
#	-Wlarger-than=4096
#	-Wsync-nand

CWARN	=					\
	-Waddress				\
	-Waggregate-return			\
	-Wall					\
	-Warray-bounds				\
	-Wbad-function-cast			\
	-Wcast-align				\
	-Wcast-qual				\
	-Wchar-subscripts			\
	-Wclobbered				\
	-Wcomment				\
	-Wconversion				\
	-Wdeclaration-after-statement		\
	-Wdisabled-optimization			\
	-Wempty-body				\
	-Werror					\
	-Wextra					\
	-Wfloat-equal				\
	-Wformat				\
	-Wformat-nonliteral			\
	-Wformat-security			\
	-Wformat-y2k				\
	-Wignored-qualifiers			\
	-Wimplicit				\
	-Winit-self				\
	-Winline				\
	-Wlogical-op				\
	-Wmain					\
	-Wmissing-braces			\
	-Wmissing-declarations			\
	-Wmissing-field-initializers		\
	-Wmissing-format-attribute		\
	-Wmissing-include-dirs			\
	-Wmissing-noreturn			\
	-Wmissing-parameter-type		\
	-Wmissing-prototypes			\
	-Wnested-externs			\
	-Wold-style-declaration			\
	-Wold-style-definition			\
	-Woverlength-strings			\
	-Woverride-init				\
	-Wpacked				\
	-Wpadded				\
	-Wparentheses				\
	-Wpointer-arith				\
	-Wpointer-sign				\
	-Wredundant-decls			\
	-Wreturn-type				\
	-Wsequence-point			\
	-Wshadow				\
	-Wsign-compare				\
	-Wsign-conversion			\
	-Wstack-protector			\
	-Wstrict-aliasing			\
	-Wstrict-aliasing=3			\
	-Wstrict-overflow			\
	-Wstrict-overflow=5			\
	-Wstrict-prototypes			\
	-Wswitch				\
	-Wswitch-default			\
	-Wswitch-enum				\
	-Wtraditional-conversion		\
	-Wtrigraphs				\
	-Wtype-limits				\
	-Wundef					\
	-Wuninitialized				\
	-Wunknown-pragmas			\
	-Wunreachable-code			\
	-Wunsafe-loop-optimizations		\
	-Wunused-function			\
	-Wunused-label				\
	-Wunused-parameter			\
	-Wunused-value				\
	-Wunused-variable			\
	-Wvariadic-macros			\
	-Wvla					\
	-Wvolatile-register-var			\
	-Wwrite-strings				\
	-pedantic-errors

ifdef BOARD
BOARD_INCLUDE	= -DADHD_BOARD_INCLUDE='"board-$(BOARD).h"'
else
BOARD_INCLUDE	= -DADHD_BOARD_INCLUDE='"board-generic.h"'
endif

INCLUDES =	-I$(ADHD_DIR)/include

CFLAGS	=					\
	-ansi					\
	$(INCLUDES)				\
	$(BOARD_INCLUDE)			\
	$(CWARN) $(COPTIONS)



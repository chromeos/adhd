# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file contains definitions that are specific to the invocation
# and usage of Gnu Make.

ifndef VERBOSE
# Be silent unless 'VERBOSE' is set on the make command line.
SILENT	= --silent
endif

ifdef BOARD
export ADHD_BUILD_DIR	= $(ADHD_DIR)/build/$(BOARD)
else
# For testing on the development desktop only.  This should not ever
# be used for a production build.  All production builds should be for
# a specific board.
export ADHD_BUILD_DIR	= $(ADHD_DIR)/build/development
endif

GAVD_ARCHIVE	= $(ADHD_BUILD_DIR)/lib/gavd.a

LIBS		=							\
		$(GAVD_ARCHIVE)						\
		$(foreach lib,$(MY_LIBS),-l$(lib))

# mkdir: Creates a directory, and all its parents, if it does not exist.
#
mkdir	= [ ! -d $(1) ] &&			\
	    $(MKDIR) --parents $(1) || true

# remake: Gnu Make function which will create the build directory,
#         then build the first argument by recursively invoking make.
#         The recursive make is performed in the build directory.
#
#         The argument to this function must be the relative pathname
#         from $(ADHD_DIR).
#
#         ex: @$(call remake,gavd)
#             @$(call remake,gavd/hypothetical_gavd_subdirectory)
#
remake	= +$(call mkdir,$(ADHD_BUILD_DIR)/$(1)) &&	\
	    echo "[$(MAKELEVEL)] Building $(1)";	\
	    $(MAKE) $(SILENT)				\
		-f $(ADHD_DIR)/$(1)/Makefile		\
		-C $(ADHD_BUILD_DIR)/$(1)		\
		VPATH=$(ADHD_DIR)/$(1)			\
		ADHD_SOURCE_DIR=$(ADHD_DIR)/$(1)	\
		$(1)

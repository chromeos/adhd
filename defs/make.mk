# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file contains definitions that are specific to the invocation
# and usage of Gnu Make.

ifndef VERBOSE
# Be silent unless 'VERBOSE' is set on the make command line.
SILENT	= --silent
endif

# This ADHD_BUILD_DIR is changed to allow multiple simultaneous
# builds, then the associated ebuild will need to be changed.  The
# Portage ebuild has been written to handle only one build directory,
# and to install files from that location directly.
#
# If multiple simultaneous build types are supported, then the logic
# to determine the output directory must be put into the ebuild.
#
export ADHD_BUILD_DIR	= $(ADHD_DIR)/build

# mkdir: Creates a directory, and all its parents, if it does not exist.
#
mkdir	= [ ! -d $(ADHD_DIR)/$(1) ] &&			\
	    $(MKDIR) --parents $(ADHD_DIR)/$(1) || true

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
remake	= +$(call mkdir,build/$(1)) &&			\
	    echo "[$(MAKELEVEL)] Building $(1)";	\
	    $(MAKE) $(SILENT)				\
		-f $(ADHD_DIR)/$(1)/Makefile		\
		-C $(ADHD_BUILD_DIR)/$(1)		\
		VPATH=$(ADHD_DIR)/$(1)			\
		$(1)

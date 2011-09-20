# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
ifndef BOARD
$(error 'BOARD' is not defined.  Unable to build gavd.)
endif

export ADHD_DIR = $(shell pwd)
include $(ADHD_DIR)/defs/definitions.mk

all:	gavd adhdinfo

adhdinfo gavd::	lib

lib gavd adhdinfo::
	@$(call remake,$@)

clean:
	@rm -rf $(ADHD_BUILD_DIR)

.PHONY:	gavd clean adhdinfo lib

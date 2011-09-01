# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
ifndef BOARD
$(warning 'BOARD' is not defined.  Board-specific features will \
	not be available.)
endif

export ADHD_DIR = $(shell pwd)
include $(ADHD_DIR)/defs/definitions.mk

all:	gavd

gavd:				# Google Audio Visual Daemon
	@$(call remake,$@)

clean:
	@rm -rf $(ADHD_BUILD_DIR)

.PHONY:	gavd clean

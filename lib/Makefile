# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
include $(ADHD_DIR)/defs/definitions.mk

CFILES	=					\
	verbose.c				\
	adhd_alsa.c

OFILES	= $(CFILES:.c=.o)

lib:	$(OFILES)
	$(AR) -r $(GAVD_ARCHIVE) $(OFILES) 2>/dev/null


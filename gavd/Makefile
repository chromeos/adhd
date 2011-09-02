# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
include $(ADHD_DIR)/defs/definitions.mk

CFILES	=					\
	main.c					\
	signal_handling.c			\
	thread_management.c			\
	thread_gpio_headphone.c

OFILES	= $(CFILES:.c=.o)
LIBS	= $(GAVD_LIB)

gavd:	$(OFILES) $(LIBS)
	$(CC) -o $@ $(OFILES) $(LIBS) \
		-Xlinker --library=pthread \
		-Xlinker --script=$(ADHD_SOURCE_DIR)/ld.script

-include *.d

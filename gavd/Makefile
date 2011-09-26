# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
include $(ADHD_DIR)/defs/definitions.mk

CFILES	=					\
	main.c					\
	signal_handling.c			\
	initialization.c			\
	set_factory_default.c			\
	sys_input.c				\
	utils.c					\
	gpio_switch_monitor.c			\
	thread_gpio_switch_monitor.c		\
	thread_management.c

OFILES	= $(CFILES:.c=.o)
MY_LIBS	= pthread asound

gavd:	$(OFILES) $(GAVD_ARCHIVE)
	$(CC) -o $@ $(OFILES) $(LIBS) -Xlinker -Map=$@.map

-include *.d

# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
include $(ADHD_DIR)/defs/definitions.mk

__dbus_includes	=				\
		/usr/include/dbus-1.0 		\
		/usr/lib64/dbus-1.0/include

DBUS_INCLUDES	=					\
		$(foreach v,$(__dbus_includes),-I$(v))

CFILES	=					\
	chrome_card_info_fifo.c			\
	codec.c					\
	codec_cirrus_cougarpoint.c		\
	codec_max98095.c			\
	codec_wm8903.c				\
	cras_connection.c			\
	device_info.c				\
	dbus_connection.c                       \
	dbus_to_chrome_fifo.c                   \
	fifo.c                                  \
	gpio_switch_monitor.c                   \
	initialization.c                        \
	set_factory_default.c                   \
	signal_handling.c                       \
	sys_input.c                             \
	thread_gpio_switch_monitor.c            \
	thread_management.c                     \
	udev_sound.c                            \
	utils.c                                 \
	workfifo.c                              \
	main.c

OFILES	= 					\
	$(CFILES:.c=.o)

MY_LIBS	=					\
	pthread					\
	asound					\
	dbus-1					\
	udev					\
	cras

CFLAGS	+=					\
	$(DBUS_INCLUDES)			\
	$(DBUS_DEFINES)

gavd:	$(OFILES) $(GAVD_ARCHIVE)
	$(CC) -o $@ $(OFILES) $(LIBS) -Xlinker -Map=$@.map

-include *.d

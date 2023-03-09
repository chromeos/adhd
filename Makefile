# Copyright 2011 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

export ADHD_DIR = $(shell pwd)
include $(ADHD_DIR)/defs/definitions.mk

all:	cras

cras_init_tmpfile:	$(ADHD_DIR)/tmpfiles.d/cras.conf
	$(ECHO) "Installing tmpfile.d file"
	$(INSTALL) --mode 644 -D $< $(DESTDIR)/usr/lib/tmpfiles.d/cras.conf
cras_init_upstart:	$(ADHD_DIR)/init/cras.conf $(ADHD_DIR)/init/cras-dev.conf
	$(ECHO) "Installing upstart file"
	$(INSTALL) --mode 644 -D $(ADHD_DIR)/init/cras.conf \
		$(DESTDIR)/etc/init/cras.conf
	$(INSTALL) --mode 644 -D $(ADHD_DIR)/init/cras-dev.conf \
		$(DESTDIR)/etc/init/cras-dev.conf

cras_init_scripts:	$(ADHD_DIR)/init/cras-env.sh \
					$(ADHD_DIR)/init/cras.sh \
					$(ADHD_DIR)/init/cras-dev.sh
	$(ECHO) "Installing init scripts"
	$(INSTALL) --mode 644 -D $(ADHD_DIR)/init/cras-env.sh \
		$(DESTDIR)/usr/share/cros/init/cras-env.sh
	$(INSTALL) --mode 644 -D $(ADHD_DIR)/init/cras.sh \
		$(DESTDIR)/usr/share/cros/init/cras.sh
	$(INSTALL) --mode 644 -D $(ADHD_DIR)/init/cras-dev.sh \
		$(DESTDIR)/usr/local/share/cros/init/cras-dev.sh

SYSTEMD_UNIT_DIR := /usr/lib/systemd/system/

cras_init_systemd:	$(ADHD_DIR)/init/cras.service
	$(ECHO) "Installing systemd files"
	$(INSTALL) --mode 644 -D $(ADHD_DIR)/init/cras.service \
		$(DESTDIR)/$(SYSTEMD_UNIT_DIR)/cras.service
	$(INSTALL) --mode 755 -d $(DESTDIR)/$(SYSTEMD_UNIT_DIR)/system-services.target.wants
	$(LINK) -s ../cras.service \
		$(DESTDIR)/$(SYSTEMD_UNIT_DIR)/system-services.target.wants/cras.service

ifeq ($(strip $(SYSTEMD)), yes)

cras_init: cras_init_systemd cras_init_scripts cras_init_tmpfile

else

cras_init: cras_init_upstart cras_init_scripts cras_init_tmpfile

endif

$(DESTDIR)/etc/cras/device_blocklist:	$(ADHD_DIR)/cras-config/device_blocklist
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

# Note: $(BOARD) usage is deprecated.  Configs should be added in board overlays
# or via cros_config data for newer unibuild systems.

optional_alsa_conf := $(wildcard $(ADHD_DIR)/alsa-module-config/alsa-$(BOARD).conf)

ifneq ($(strip $(optional_alsa_conf)),)

$(DESTDIR)/etc/modprobe.d/alsa-$(BOARD).conf:	$(optional_alsa_conf)
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

install:	$(DESTDIR)/etc/modprobe.d/alsa-$(BOARD).conf

endif

optional_alsa_patch := $(wildcard $(ADHD_DIR)/alsa-module-config/$(BOARD)_alsa.fw)

ifneq ($(strip $(optional_alsa_patch)),)

$(DESTDIR)/lib/firmware/$(BOARD)_alsa.fw:	$(optional_alsa_patch)
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

install:	$(DESTDIR)/lib/firmware/$(BOARD)_alsa.fw

endif

install:	$(DESTDIR)/etc/cras/device_blocklist \
		cras_install \
		cras_init

clean:
	@rm -rf $(ADHD_BUILD_DIR)

.PHONY:	clean cras cras_install

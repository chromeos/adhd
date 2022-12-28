# Copyright 2011 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

export ADHD_DIR = $(shell pwd)
include $(ADHD_DIR)/defs/definitions.mk

all:	cras

ifeq ($(strip $(BAZEL)), yes)

# Do nothing. Handled by bazel.
cras:
cras-scripts:

else

cras:
	@$(call remake,Building,$@,cras.mk,$@)

cras_install:
	@$(call remake,Building,cras,cras.mk,$@)

endif

cras-scripts:
	$(ECHO) "Installing cras scripts"
	$(INSTALL) --mode 755 -d $(DESTDIR)/usr/bin/
	$(INSTALL) --mode 755 -D $(ADHD_DIR)/scripts/audio_diagnostics \
		$(DESTDIR)/usr/bin/
	$(INSTALL) --mode 755 -D $(ADHD_DIR)/scripts/asoc_dapm_graph \
		$(DESTDIR)/usr/bin/

cras_init_tmpfile:	$(ADHD_DIR)/tmpfiles.d/cras.conf
	$(ECHO) "Installing tmpfile.d file"
	$(INSTALL) --mode 644 -D $< $(DESTDIR)/usr/lib/tmpfiles.d/cras.conf
cras_init_upstart:	$(ADHD_DIR)/init/cras.conf
	$(ECHO) "Installing upstart file"
	$(INSTALL) --mode 644 -D $< $(DESTDIR)/etc/init/cras.conf

cras_init_scripts:	$(ADHD_DIR)/init/cras.sh
	$(INSTALL) --mode 644 -D $< $(DESTDIR)/usr/share/cros/init/cras.sh

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
		cras-scripts \
		cras_install \
		cras_init

clean:
	@rm -rf $(ADHD_BUILD_DIR)

.PHONY:	clean cras cras_install cras-script

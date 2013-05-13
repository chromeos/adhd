# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
ifndef BOARD
$(error 'BOARD' is not defined.  Unable to build ADHD.)
endif

export ADHD_DIR = $(shell pwd)
include $(ADHD_DIR)/defs/definitions.mk

all:	cras

cras:
	@$(call remake,Building,$@,cras.mk,$@)

cras_install:
	@$(call remake,Building,cras,cras.mk,$@)

$(DESTDIR)/etc/init/cras.conf:	$(ADHD_DIR)/upstart/cras.conf
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

optional_asound_state := $(wildcard $(ADHD_DIR)/factory-default/asound.state.$(BOARD))

ifneq ($(strip $(optional_asound_state)),)
asound_state := $(ADHD_DIR)/factory-default/asound.state.$(BOARD)
else
asound_state := $(ADHD_DIR)/factory-default/asound.state.empty
endif

$(DESTDIR)/etc/asound.state: $(asound_state)
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

$(DESTDIR)/etc/cras/device_blacklist:	$(ADHD_DIR)/cras-config/device_blacklist
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

optional_alsa_conf := $(wildcard $(ADHD_DIR)/alsa-module-config/alsa-$(BOARD).conf)

ifneq ($(strip $(optional_alsa_conf)),)

$(DESTDIR)/etc/modprobe.d/alsa-$(BOARD).conf:	$(optional_alsa_conf)
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

install:	$(DESTDIR)/etc/modprobe.d/alsa-$(BOARD).conf

endif

optional_cras_conf := $(wildcard $(ADHD_DIR)/cras-config/$(BOARD)/*)

ifneq ($(strip $(optional_cras_conf)),)

.PHONY: cras-config-files cras-scripts
cras-config-files:
	$(ECHO) "Installing cras config files"
	$(INSTALL) --mode 755 -d $(DESTDIR)etc/cras/
	$(INSTALL) --mode 644 -D $(ADHD_DIR)/cras-config/$(BOARD)/* $(DESTDIR)etc/cras/

cras-scripts:
	$(ECHO) "Installing cras scripts"
	$(INSTALL) --mode 755 -d $(DESTDIR)usr/bin/
	$(INSTALL) --mode 755 -D $(ADHD_DIR)/scripts/audio_diagnostics \
		$(DESTDIR)usr/bin/

install:	cras-config-files cras-scripts

endif

install:	$(DESTDIR)/etc/init/cras.conf				\
		$(DESTDIR)/etc/asound.state				\
		$(DESTDIR)/etc/cras/device_blacklist			\
		cras_install
clean:
	@rm -rf $(ADHD_BUILD_DIR)

.PHONY:	clean cras cras_install

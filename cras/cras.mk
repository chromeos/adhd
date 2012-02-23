# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#include $(ADHD_DIR)/defs/definitions.mk
CWARN=
CFLAGS=

cras:
	(cd $(ADHD_DIR)/cras		&&	\
	$(ADHD_DIR)/cras/git_prepare.sh	&&	\
	$(ADHD_DIR)/cras/configure		\
	    --build=$(CBUILD)			\
	    --host=$(CHOST)			\
	    --prefix=/usr			\
	    --mandir=/usr/share/man		\
	    --infodir=/usr/share/info		\
	    --datadir=/usr/share		\
	    --sysconfdir=/etc			\
	    --localstatedir=/var/lib	&&	\
	$(MAKE) -f $(ADHD_DIR)/cras/Makefile)

cras_install:
	(cd $(ADHD_DIR)/cras		&&	\
	$(MAKE) -f $(ADHD_DIR)/cras/Makefile install)


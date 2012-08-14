/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <alsa/use-case.h>

#include "cras_alsa_ucm.h"

const char DefaultVerb[] = "HiFi";

snd_use_case_mgr_t *ucm_create(const char *name)
{
	snd_use_case_mgr_t *mgr;
	int rc;

	if (!name)
		return NULL;

	rc = snd_use_case_mgr_open(&mgr, name);
	if (rc)
		return NULL;

	rc = snd_use_case_set(mgr, "_verb", DefaultVerb);
	if (rc) {
		ucm_destroy(mgr);
		return NULL;
	}

	return mgr;
}

void ucm_destroy(snd_use_case_mgr_t *mgr)
{
	snd_use_case_mgr_close(mgr);
}

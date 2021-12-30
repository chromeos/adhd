/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_hats.h"

#ifdef HAVE_HATS

#include "cras_observer.h"

void cras_hats_trigger_general_survey(enum CRAS_STREAM_TYPE stream_type,
				      enum CRAS_CLIENT_TYPE client_type,
				      const char *node_type_pair)
{
	cras_observer_notify_general_survey(stream_type, client_type,
					    node_type_pair);
}

#else
void cras_hats_trigger_general_survey(enum CRAS_STREAM_TYPE stream_type,
				      enum CRAS_CLIENT_TYPE client_type,
				      const char *node_type_pair)
{
}
#endif

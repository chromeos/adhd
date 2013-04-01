/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <syslog.h>
#include <unistd.h>

void cras_metrics_log_event(const char *event)
{
	syslog(LOG_DEBUG, "Log event: %s", event);
	if (!fork()) {
		const char *argv[] = {"metrics_client", "-v", event, NULL} ;
		execvp(argv[0], (char * const *)argv);
		_exit(1);
	}
}

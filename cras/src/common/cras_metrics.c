/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <syslog.h>

#ifdef _WITH_CHROMEOS_METRICS
#include <metrics/c_metrics_library.h>

CMetricsLibrary metrics_lib;
#endif

int cras_metrics_init()
{
#ifdef _WITH_CHROMEOS_METRICS
	metrics_lib = CMetricsLibraryNew();
	if (!metrics_lib) {
		syslog(LOG_ERR, "Failed to create ChromeOS metrics library.");
		return -ENOMEM;
	}

	CMetricsLibraryInit(metrics_lib);
#endif
	return 0;
}

void cras_metrics_deinit()
{
#ifdef _WITH_CHROMEOS_METRICS
	if (metrics_lib)
		CMetricsLibraryDelete(metrics_lib);
#endif
}

void cras_metrics_log_action(const char *action)
{
	syslog(LOG_DEBUG, "Metric: %s", action);

#ifdef _WITH_CHROMEOS_METRICS
	if (metrics_lib)
		CMetricsLibrarySendUserActionToUMA(metrics_lib, action);
#endif
}

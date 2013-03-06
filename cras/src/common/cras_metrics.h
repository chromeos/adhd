/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_METRICS_H_
#define CRAS_METRICS_H_

/* Initializes the metrics facility. */
int cras_metrics_init();

/* Deinitialize the metrics facility. */
void cras_metrics_deinit();

/* Logs the specified action. */
void cras_metrics_log_action(const char *action);

#endif /* CRAS_METRICS_H_ */

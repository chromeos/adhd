/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdlib.h>

#include "cras_alert.h"
#include "utlist.h"

/* A list of callbacks for an alert */
struct cras_alert_cb_list {
	cras_alert_cb callback;
	void *arg;
	struct cras_alert_cb_list *prev, *next;
};

struct cras_alert {
	int pending;
	cras_alert_prepare prepare;
	struct cras_alert_cb_list *callbacks;
	struct cras_alert *prev, *next;
};

/* A list of all alerts in the system */
static struct cras_alert *all_alerts;
/* If there is any alert pending. */
static int has_alert_pending;

struct cras_alert *cras_alert_create(cras_alert_prepare prepare)
{
	struct cras_alert *alert;
	alert = calloc(1, sizeof(*alert));
	if (!alert)
		return NULL;
	alert->prepare = prepare;
	DL_APPEND(all_alerts, alert);
	return alert;
}

int cras_alert_add_callback(struct cras_alert *alert, cras_alert_cb cb,
			    void *arg)
{
	struct cras_alert_cb_list *alert_cb;

	if (cb == NULL)
		return -EINVAL;

	DL_FOREACH(alert->callbacks, alert_cb)
		if (alert_cb->callback == cb && alert_cb->arg == arg)
			return -EEXIST;

	alert_cb = calloc(1, sizeof(*alert_cb));
	if (alert_cb == NULL)
		return -ENOMEM;
	alert_cb->callback = cb;
	alert_cb->arg = arg;
	DL_APPEND(alert->callbacks, alert_cb);
	return 0;
}

int cras_alert_rm_callback(struct cras_alert *alert, cras_alert_cb cb,
			   void *arg)
{
	struct cras_alert_cb_list *alert_cb, *tmp;

	DL_FOREACH_SAFE(alert->callbacks, alert_cb, tmp)
		if (alert_cb->callback == cb && alert_cb->arg == arg) {
			DL_DELETE(alert->callbacks, alert_cb);
			free(alert_cb);
			return 0;
		}
	return -ENOENT;
}

/* Checks if the alert is pending, and invoke the prepare function and callbacks
 * if so. */
static void cras_alert_process(struct cras_alert *alert)
{
	struct cras_alert_cb_list *cb, *tmp;

	if (alert->pending) {
		alert->pending = 0;
		if (alert->prepare)
			alert->prepare(alert);
		DL_FOREACH_SAFE(alert->callbacks, cb, tmp)
			cb->callback(cb->arg);
	}
}

void cras_alert_pending(struct cras_alert *alert)
{
	alert->pending = 1;
	has_alert_pending = 1;
}

void cras_alert_process_all_pending_alerts()
{
	struct cras_alert *alert, *tmp;

	while (has_alert_pending) {
		has_alert_pending = 0;
		DL_FOREACH_SAFE(all_alerts, alert, tmp)
			cras_alert_process(alert);
	}
}

void cras_alert_destroy(struct cras_alert *alert)
{
	struct cras_alert_cb_list *cb, *tmp;

	if (!alert)
		return;

	DL_FOREACH_SAFE(alert->callbacks, cb, tmp) {
		DL_DELETE(alert->callbacks, cb);
		free(cb);
	}

	alert->callbacks = NULL;
	DL_DELETE(all_alerts, alert);
	free(alert);
}

void cras_alert_destroy_all()
{
	struct cras_alert *alert, *tmp;
	DL_FOREACH_SAFE(all_alerts, alert, tmp)
		cras_alert_destroy(alert);
}

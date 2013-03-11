/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CRAS_ALERT_H
#define _CRAS_ALERT_H

#ifdef __cplusplus
extern "C" {
#endif

/* The alert facility provides a way to signal the clients when a system state
 * changes.
 *
 * First the clients registers callbacks to an alert. Each time the system state
 * changes, we mark the associated alert as "pending". At the end of the event
 * loop, we invoke the callbacks for the pending alerts.
 *
 * We do this delayed callback to collapses multiple callbacks into one (for
 * example, if there are multiple nodes added at the same time, we will only
 * fire the "nodes changed" signal once).
 *
 * There is an optional "prepare" function which can be provided when creating
 * an alert. It is called before we invoke the callbacks. This gives the owner
 * of each alert a chance to update the system to a consistent state before
 * signalling the clients.
 *
 * The alert functions should only be used from the main thread.
 */

struct cras_alert;

/* Callback functions to be notified when settings change. arg is a user
 * provided argument that will be passed back. */
typedef void (*cras_alert_cb)(void *arg);
typedef void (*cras_alert_prepare)(struct cras_alert *alert);

/* Creates an alert.
 * Args:
 *    prepare - A function which will be called before calling the callbacks.
 *        The prepare function should update the system state in the shared
 *        memory to be consistent. It can be NULL if not needed.
 * Returns:
 *    A pointer to the alert, NULL if out of memory.
 */
struct cras_alert *cras_alert_create(cras_alert_prepare prepare);

/* Adds a callback to the alert.
 * Args:
 *    alert - A pointer to the alert.
 *    cb - The callback.
 *    arg - will be passed to the callback when it is called.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int cras_alert_add_callback(struct cras_alert *alert, cras_alert_cb cb,
			    void *arg);

/* Removes a callback from the alert. It removes the callback if cb and arg
 * match a previously registered entry.
 * Args:
 *    alert - A pointer to the alert.
 *    cb - The callback.
 *    arg - will be passed to the callback when it is called.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int cras_alert_rm_callback(struct cras_alert *alert, cras_alert_cb cb,
			   void *arg);

/* Marks an alert as pending. We don't call the callbacks immediately when an
 * alert becomes pending, but will do that when
 * cras_alert_process_all_pending_alerts() is called.
 * Args:
 *    alert - A pointer to the alert.
 */
void cras_alert_pending(struct cras_alert *alert);

/* Processes all alerts that are pending.
 *
 * For all pending alerts, its prepare function will be called, then the
 * callbacks will be called. If any alert becomes pending during the callbacks,
 * the process will start again until no alert is pending.
 */
void cras_alert_process_all_pending_alerts();

/* Frees the resources used by an alert.
 * Args:
 *    alert - A pointer to the alert.
 */
void cras_alert_destroy(struct cras_alert *alert);

/* Frees the resources used by all alerts in the system. */
void cras_alert_destroy_all();

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _CRAS_ALERT_H */

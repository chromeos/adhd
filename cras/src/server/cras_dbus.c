/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <dbus/dbus.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "cras_system_state.h"

struct dbus_timeout_callback_data_t {
	DBusTimeout *timeout;
	int fd;
};

static void dbus_watch_callback(void *arg)
{
	DBusWatch *watch = (DBusWatch *)arg;
	int fd, r, flags;
	fd_set readfds, writefds;
	struct timeval timeout;

	fd = dbus_watch_get_unix_fd(watch);

	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);

	FD_ZERO(&writefds);
	FD_SET(fd, &writefds);

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	r = select(fd + 1, &readfds, &writefds, NULL, &timeout);
	if (r <= 0)
		return;

	flags = 0;
	if (FD_ISSET(fd, &readfds))
		flags |= DBUS_WATCH_READABLE;
	if (FD_ISSET(fd, &writefds))
		flags |= DBUS_WATCH_WRITABLE;

	if (!dbus_watch_handle(watch, flags))
		syslog(LOG_WARNING, "Failed to handle D-Bus watch.");
}

static dbus_bool_t dbus_watch_add(DBusWatch *watch, void *data)
{
	int r;

	if (dbus_watch_get_enabled(watch)) {
		r = cras_system_add_select_fd(dbus_watch_get_unix_fd(watch),
					      dbus_watch_callback,
					      watch);
		if (r != 0)
			return FALSE;
	}

	return TRUE;
}

static void dbus_watch_remove(DBusWatch *watch, void *data)
{
	cras_system_rm_select_fd(dbus_watch_get_unix_fd(watch));
}

static void dbus_watch_toggled(DBusWatch *watch, void *data)
{
	if (dbus_watch_get_enabled(watch)) {
		dbus_watch_add(watch, NULL);
	} else {
		dbus_watch_remove(watch, NULL);
	}
}


static void dbus_timeout_callback(void *arg)
{
	struct dbus_timeout_callback_data_t *data
			= (struct dbus_timeout_callback_data_t *)arg;
	int r;
	uint64_t expirations;

	r = read(data->fd, &expirations, sizeof(expirations));
	if (r < 0)
		syslog(LOG_WARNING, "Failed to read from D-Bus timer: %m");
	else if (r < sizeof(expirations))
		syslog(LOG_WARNING, "Short read from D-Bus timer.");

	if (!dbus_timeout_handle(data->timeout))
		syslog(LOG_WARNING, "Failed to handle D-Bus timeout.");
}

static dbus_bool_t dbus_timeout_add(DBusTimeout *timeout, void *arg)
{
	struct dbus_timeout_callback_data_t *data;
	int r;

	data = calloc(1, sizeof(*data));
	if (data == NULL)
		return FALSE;

	data->timeout = timeout;
	data->fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	if (data->fd < 0)
		goto error;

	r = cras_system_add_select_fd(data->fd,
				      dbus_timeout_callback,
				      data);
	if (r != 0)
		goto error;

	if (dbus_timeout_get_enabled(data->timeout)) {
		struct itimerspec value;
		int interval;

		interval = dbus_timeout_get_interval(timeout);

		value.it_value.tv_sec = interval / 1000;
		value.it_value.tv_nsec = (interval % 1000) * 1000;

		value.it_interval.tv_sec = 0;
		value.it_interval.tv_nsec = 0;

		r = timerfd_settime(data->fd, 0, &value, NULL);
		if (r < 0)
			goto error;
	}

	dbus_timeout_set_data(timeout, data, free);

	return TRUE;

error:
	{
		int saved_errno = errno;
		if (data->fd >= 0) {
			cras_system_rm_select_fd(data->fd);
			close(data->fd);
		}
		free(data);
		errno = saved_errno;
		return FALSE;
	}
}

static void dbus_timeout_remove(DBusTimeout *timeout, void *arg)
{
	struct dbus_timeout_callback_data_t *data;
	int r;

	data = dbus_timeout_get_data(timeout);

	cras_system_rm_select_fd(data->fd);

	r = close(data->fd);
	if (r < 0)
		syslog(LOG_WARNING, "Failed to close D-Bus timer: %m");
}

static void dbus_timeout_toggled(DBusTimeout *timeout, void *arg)
{
	struct dbus_timeout_callback_data_t *data;
	struct itimerspec value;
	int r;

	data = dbus_timeout_get_data(timeout);

	if (dbus_timeout_get_enabled(data->timeout)) {
		int interval;

		interval = dbus_timeout_get_interval(timeout);

		value.it_value.tv_sec = interval / 1000;
		value.it_value.tv_nsec = (interval % 1000) * 1000;

		value.it_interval.tv_sec = 0;
		value.it_interval.tv_nsec = 0;
	} else {
		value.it_value.tv_sec = 0;
		value.it_value.tv_nsec = 0;

		value.it_interval.tv_sec = 0;
		value.it_interval.tv_nsec = 0;
	}

	r = timerfd_settime(data->fd, 0, &value, NULL);
	if (r < 0)
		syslog(LOG_WARNING, "Failed to toggle D-Bus timer: %m");
}


static DBusConnection *dbus_conn;
void cras_dbus_connect_system_bus()
{
	DBusError dbus_error;

	dbus_error_init(&dbus_error);

	dbus_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &dbus_error);
	if (!dbus_conn) {
		syslog(LOG_WARNING, "Failed to connect to D-Bus: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		return;
	}

	if (!dbus_connection_set_watch_functions(dbus_conn,
						 dbus_watch_add,
						 dbus_watch_remove,
						 dbus_watch_toggled,
						 NULL,
						 NULL))
		goto error;
	if (!dbus_connection_set_timeout_functions(dbus_conn,
						   dbus_timeout_add,
						   dbus_timeout_remove,
						   dbus_timeout_toggled,
						   NULL,
						   NULL))
		goto error;

	return;

error:
	syslog(LOG_WARNING, "Failed to setup D-Bus connection.");
	dbus_connection_unref(dbus_conn);
	dbus_conn = NULL;
}

DBusConnection *cras_dbus_system_bus(void)
{
	return dbus_conn;
}

void cras_dbus_dispatch(void)
{
	while (dbus_connection_dispatch(dbus_conn)
		== DBUS_DISPATCH_DATA_REMAINS)
		;
}

void cras_dbus_disconnect_system_bus(void)
{
	dbus_connection_unref(dbus_conn);
	dbus_conn = NULL;
}

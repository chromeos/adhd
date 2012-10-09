/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <dbus/dbus.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
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
	int fd, r, flags;
	fd_set readfds, writefds;
	struct timeval timeout;
	DBusWatch *watch = (DBusWatch *)arg;
	assert(watch != NULL);

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

	dbus_watch_handle(watch, flags);
}

static dbus_bool_t dbus_watch_add(DBusWatch *watch, void *data)
{
	int r;

	assert(watch != NULL);

	if (dbus_watch_get_enabled(watch)) {
		r = cras_system_add_select_fd(dbus_watch_get_unix_fd(watch),
					      dbus_watch_callback,
					      watch);
		assert(r == 0);
	}

	return TRUE;
}

static void dbus_watch_remove(DBusWatch *watch, void *data)
{
	assert(watch != NULL);

	cras_system_rm_select_fd(dbus_watch_get_unix_fd(watch));
}

static void dbus_watch_toggled(DBusWatch *watch, void *data)
{
	int r;

	assert(watch != NULL);

	if (dbus_watch_get_enabled(watch)) {
		r = cras_system_add_select_fd(dbus_watch_get_unix_fd(watch),
					      dbus_watch_callback,
					      watch);
		assert(r == 0);
	} else {
		cras_system_rm_select_fd(dbus_watch_get_unix_fd(watch));
	}
}


static void dbus_timeout_callback(void *arg)
{
	int r;
	uint64_t expirations;
	struct dbus_timeout_callback_data_t *data
			= (struct dbus_timeout_callback_data_t *)arg;
	assert(data != NULL);

	r = read(data->fd, &expirations, sizeof(expirations));
	if (r < sizeof(expirations))
		return;

	if (!dbus_timeout_handle(data->timeout)) {
		struct itimerspec value;
		int interval;

		interval = dbus_timeout_get_interval(data->timeout);

		value.it_value.tv_sec = interval / 1000;
		value.it_value.tv_nsec = (interval % 1000) * 1000;

		value.it_interval.tv_sec = 0;
		value.it_interval.tv_nsec = 0;

		r = timerfd_settime(data->fd, 0, &value, NULL);
		assert(r == 0);
	}
}

static dbus_bool_t dbus_timeout_add(DBusTimeout *timeout, void *arg)
{
	struct dbus_timeout_callback_data_t *data;
	int r;

	assert(timeout != NULL);
	assert(dbus_timeout_get_data(timeout) == NULL);

	data = calloc(1, sizeof(*data));
	if (data == NULL)
		return FALSE;

	data->timeout = timeout;
	data->fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	if (data->fd < 0) {
		int saved_errno = errno;
		free(data);
		errno = saved_errno;
		return FALSE;
	}

	r = cras_system_add_select_fd(data->fd,
				      dbus_timeout_callback,
				      data);
	if (r != 0) {
		int saved_errno = errno;
		close(data->fd);
		free(data);
		errno = saved_errno;
		return FALSE;
	}

	if (dbus_timeout_get_enabled(data->timeout)) {
		struct itimerspec value;
		int interval;

		interval = dbus_timeout_get_interval(timeout);

		value.it_value.tv_sec = interval / 1000;
		value.it_value.tv_nsec = (interval % 1000) * 1000;

		value.it_interval.tv_sec = 0;
		value.it_interval.tv_nsec = 0;

		r = timerfd_settime(data->fd, 0, &value, NULL);
		assert(r == 0);
	}

	dbus_timeout_set_data(timeout, data, free);

	return TRUE;
}

static void dbus_timeout_remove(DBusTimeout *timeout, void *arg)
{
	struct dbus_timeout_callback_data_t *data;

	assert(timeout != NULL);
	data = dbus_timeout_get_data(timeout);
	assert(data != NULL);

	close(data->fd);
}

static void dbus_timeout_toggled(DBusTimeout *timeout, void *arg)
{
	struct dbus_timeout_callback_data_t *data;
	int r;

	assert(timeout != NULL);
	data = dbus_timeout_get_data(timeout);
	assert(data != NULL);

	struct itimerspec value;
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
	assert(r == 0);
}


static DBusConnection *conn;
void cras_dbus_connect_system_bus()
{
	int r;

	conn = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
	assert(conn != NULL);

	r = dbus_connection_set_watch_functions(conn,
						dbus_watch_add,
						dbus_watch_remove,
						dbus_watch_toggled,
						NULL,
						NULL);
	assert(r != FALSE);

	r = dbus_connection_set_timeout_functions(conn,
						  dbus_timeout_add,
						  dbus_timeout_remove,
						  dbus_timeout_toggled,
						  NULL,
						  NULL);
	assert(r == 0);
}

DBusConnection *cras_dbus_system_bus(void)
{
	return conn;
}

void cras_udev_disconnect_system_bus(void)
{
	dbus_connection_close(conn);
	conn = NULL;
}

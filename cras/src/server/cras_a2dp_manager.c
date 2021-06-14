/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* for ppoll */
#endif

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>

#include "cras_a2dp_manager.h"
#include "cras_a2dp_pcm_iodev.h"
#include "cras_bt_log.h"
#include "cras_config.h"
#include "cras_iodev.h"
#include "cras_system_state.h"
#include "cras_tm.h"

/* Pointers for the object representing the only connected a2dp device.
 * Members:
 *    iodev - The connected a2dp iodev.
 *    skt_fd - The socket fd to the a2dp device.
 *    suspend_reason - The reason code for why suspend is scheduled.
 */
static struct a2dp {
	struct cras_iodev *iodev;
	int skt_fd;
	struct cras_timer *suspend_timer;
} connected_a2dp;

void fill_local_a2dp_skt_addr(struct sockaddr_un *addr)
{
	memset(addr, 0, sizeof(struct sockaddr_un));
	addr->sun_family = AF_UNIX;
	snprintf(addr->sun_path, CRAS_MAX_SOCKET_PATH_SIZE, "%s/%s",
		 cras_config_get_system_socket_file_dir(),
		 CRAS_A2DP_SOCKET_FILE);
}

void fill_floss_a2dp_skt_addr(struct sockaddr_un *addr)
{
	memset(addr, 0, sizeof(struct sockaddr_un));
	addr->sun_family = AF_UNIX;
}

void cras_a2dp_suspend_connected_iodev()
{
	if (connected_a2dp.iodev == NULL)
		return;

	syslog(LOG_INFO, "Destroying iodev for A2DP device");
	a2dp_pcm_iodev_destroy(connected_a2dp.iodev);
	connected_a2dp.iodev = NULL;
	connected_a2dp.skt_fd = -1;
}

void cras_floss_a2dp_start()
{
	BTLOG(btlog, BT_A2DP_START, 0, 0);

	if (connected_a2dp.iodev) {
		syslog(LOG_WARNING,
		       "Replacing existing endpoint configuration");
		a2dp_pcm_iodev_destroy(connected_a2dp.iodev);
	}

	connected_a2dp.iodev = a2dp_pcm_iodev_create();
	connected_a2dp.skt_fd = -1;
	if (!connected_a2dp.iodev)
		syslog(LOG_WARNING, "Failed to create a2dp iodev");
}

void cras_floss_a2dp_stop()
{
	cras_a2dp_suspend_connected_iodev();
	cras_a2dp_skt_release();
}

int cras_a2dp_skt_release()
{
	int fd = connected_a2dp.skt_fd;
	if (fd < 0)
		return 0;

	connected_a2dp.skt_fd = -1;
	return close(fd);
}

int cras_a2dp_skt_acquire()
{
	int skt_fd = -1;
	int rc;
	struct sockaddr_un addr;
	struct timespec timeout = { 1, 0 };
	struct pollfd poll_fd;

	cras_a2dp_skt_release();

	skt_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (skt_fd < 0) {
		syslog(LOG_ERR, "A2DP socket failed");
		return skt_fd;
	}

	fill_floss_a2dp_skt_addr(&addr);

	rc = connect(skt_fd, (struct sockaddr *)&addr, sizeof(addr));
	if (rc < 0) {
		syslog(LOG_ERR, "Connect to A2DP socket failed");
		goto error;
	}

	poll_fd.fd = skt_fd;
	poll_fd.events = POLLOUT;

	rc = ppoll(&poll_fd, 1, &timeout, NULL);
	if (rc <= 0) {
		syslog(LOG_ERR, "Poll for A2DP socket failed");
		goto error;
	}

	if (poll_fd.revents & (POLLERR | POLLHUP)) {
		syslog(LOG_ERR,
		       "A2DP socket error, revents: %u. Suspend in %u seconds",
		       poll_fd.revents, CRAS_A2DP_SUSPEND_DELAY_MS);
		cras_a2dp_schedule_suspend(CRAS_A2DP_SUSPEND_DELAY_MS);
		rc = -1;
		goto error;
	}

	connected_a2dp.skt_fd = skt_fd;
	return skt_fd;

error:
	if (skt_fd) {
		close(skt_fd);
		unlink(addr.sun_path);
	}
	return rc;
}

static void a2dp_suspend_cb(struct cras_timer *timer, void *arg)
{
	cras_a2dp_suspend_connected_iodev();
}

void cras_a2dp_schedule_suspend(unsigned int msec)
{
	struct cras_tm *tm;

	if (connected_a2dp.suspend_timer)
		return;

	tm = cras_system_state_get_tm();
	connected_a2dp.suspend_timer =
		cras_tm_create_timer(tm, msec, a2dp_suspend_cb, NULL);
}

void cras_a2dp_cancel_suspend()
{
	struct cras_tm *tm;

	if (connected_a2dp.suspend_timer == NULL)
		return;

	tm = cras_system_state_get_tm();
	cras_tm_cancel_timer(tm, connected_a2dp.suspend_timer);
	connected_a2dp.suspend_timer = NULL;
}

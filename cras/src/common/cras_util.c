/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "cras_config.h"
#include "cras_types.h"

int cras_set_rt_scheduling(int rt_lim)
{
	struct rlimit rl;

	rl.rlim_cur = rl.rlim_max = rt_lim;

	if (setrlimit(RLIMIT_RTPRIO, &rl) < 0) {
		syslog(LOG_WARNING, "setrlimit %u failed: %d\n",
		       (unsigned) rt_lim, errno);
		return -EACCES;
	}

	syslog(LOG_INFO, "set rlimit success\n");
	return 0;
}

int cras_set_thread_priority(int priority)
{
	struct sched_param sched_param;
	int err;

	memset(&sched_param, 0, sizeof(sched_param));
	sched_param.sched_priority = priority;

	err = pthread_setschedparam(pthread_self(), SCHED_RR, &sched_param);
	if (err < 0)
		syslog(LOG_WARNING, "Set sched params for thread\n");

	return err;
}

int cras_set_nice_level(int nice)
{
	int rc;

	/* Linux isn't posix compliant with setpriority(2), it will set a thread
	 * priority if it is passed a tid, not affecting the rest of the threads
	 * in the process.  Setting this priority will only succeed if the user
	 * has been granted permission to adjust nice values on the system.
	 */
	rc = setpriority(PRIO_PROCESS, syscall(__NR_gettid), nice);
	syslog(LOG_DEBUG, "Set nice to %d %s.", nice, rc ? "Fail" : "Success");

	return rc;
}

int cras_server_connect_to_client_socket(cras_stream_id_t stream_id)
{
	struct sockaddr_un addr;
	int  fd;
	int err;
	const char *sockdir;

	/* Build the name of the socket from the path and stream id. */
	sockdir = cras_config_get_system_socket_file_dir();
	assert(sockdir);

	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, 50, "%s/%s-%x", sockdir, CRAS_AUD_FILE_PATTERN,
		 stream_id);
	syslog(LOG_DEBUG, "Connect to %s\n", addr.sun_path);

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		syslog(LOG_ERR, "socket failed");
		return fd;
	}

	err = connect(fd, (struct sockaddr *) &addr, sizeof(addr));
	if (err != 0) {
		syslog(LOG_ERR, "connect to client socket %x", stream_id);
		return err;
	}

	return fd;
}

int cras_server_disconnect_from_client_socket(int socket_fd)
{
	return close(socket_fd);
}

int cras_make_fd_nonblocking(int fd)
{
	int fl;

	fl = fcntl(fd, F_GETFL);
	if (fl < 0)
		return fl;
	if (fl & O_NONBLOCK)
		return 0;
	return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

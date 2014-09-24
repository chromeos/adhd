/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <getopt.h>
#include <signal.h>
#include <syslog.h>

#include "cras_config.h"
#include "cras_iodev_list.h"
#include "cras_loopback_iodev.h"
#include "cras_server.h"
#include "cras_system_state.h"
#include "cras_dsp.h"

static int enable_hfp;

static struct option long_options[] = {
	{"enable_hfp", no_argument, &enable_hfp, 1},
	{0, 0, 0, 0}
};

/* Ignores sigpipe, we'll notice when a read/write fails. */
static void set_signals()
{
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);
}

/* Entry point for the server. */
int main(int argc, char **argv)
{
	int c, option_index;

	setlogmask(LOG_MASK(LOG_ERR));

	set_signals();

	while (1) {
		c = getopt_long(argc, argv, "", long_options, &option_index);
		if (c == -1)
			break;
	}

	/* Initialize system. */
	cras_server_init();
	cras_system_state_init();
	cras_dsp_init(CRAS_CONFIG_FILE_DIR "/dsp.ini");
	cras_iodev_list_init();

	/* Add loopback device for capturing the post-mix system output. */
	loopback_iodev_create(CRAS_STREAM_POST_MIX_PRE_DSP);

	/* Start the server. */
	cras_server_run(enable_hfp);

	return 0;
}

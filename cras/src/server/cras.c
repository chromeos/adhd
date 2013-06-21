/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <signal.h>
#include <syslog.h>

#include "cras_config.h"
#include "cras_empty_iodev.h"
#include "cras_iodev_list.h"
#include "cras_loopback_iodev.h"
#include "cras_server.h"
#include "cras_system_state.h"
#include "cras_dsp.h"

/* Ignores sigpipe, we'll notice when a read/write fails. */
static void set_signals()
{
	signal(SIGPIPE, SIG_IGN);
}

/* Entry point for the server. */
int main(int argc, char **argv)
{
	setlogmask(LOG_MASK(LOG_ERR));

	set_signals();

	/* Initialize system. */
	cras_system_state_init();
	cras_dsp_init(CRAS_CONFIG_FILE_DIR "/dsp.ini");
	cras_iodev_list_init();

	/* Add an empty device so there is always something to play to or
	 * capture from. */
	empty_iodev_create(CRAS_STREAM_OUTPUT);
	empty_iodev_create(CRAS_STREAM_INPUT);

	/* Add loopback device for capturing the post-mix system output. */
	loopback_iodev_create(CRAS_STREAM_POST_MIX_PRE_DSP);

	/* Start the server. */
	cras_server_run();

	return 0;
}

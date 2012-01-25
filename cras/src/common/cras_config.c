/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pwd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

const char *cras_config_get_user_homedir()
{
	const char *dir;
	struct passwd *pw;

	dir = getenv("HOME");
	if (dir)
		return dir;

	pw = getpwuid(getuid());
	if (!pw)
		return NULL;

	return pw->pw_dir;
}

const char *cras_config_get_socket_file_dir()
{
	return cras_config_get_user_homedir();
}

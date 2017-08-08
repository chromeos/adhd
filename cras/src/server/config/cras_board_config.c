/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <iniparser.h>
#include <syslog.h>

#include "cras_board_config.h"

/* Allocate 63 chars + 1 for null where declared. */
static const unsigned int MAX_INI_NAME_LEN = 63;
static const unsigned int MAX_KEY_LEN = 63;
static const int32_t DEFAULT_MIN_OUTPUT_BUFFER_SIZE = 512;

#define CONFIG_NAME "board.ini"
#define INI_KEY_NAME "output:min_output_buffer_size"


void cras_board_config_get(const char *config_path,
		struct cras_board_config *board_config)
{
	char ini_name[MAX_INI_NAME_LEN + 1];
	char ini_key[MAX_KEY_LEN + 1];
	dictionary *ini;

	board_config->min_output_buffer_size = DEFAULT_MIN_OUTPUT_BUFFER_SIZE;
	if (config_path == NULL)
		return;

	snprintf(ini_name, MAX_INI_NAME_LEN, "%s/%s", config_path,
		CONFIG_NAME);
	ini_name[MAX_INI_NAME_LEN] = '\0';
	ini = iniparser_load(ini_name);
	if (ini == NULL) {
		syslog(LOG_DEBUG, "No ini file %s", ini_name);
		return;
	}

	snprintf(ini_key, MAX_KEY_LEN, INI_KEY_NAME);
	ini_key[MAX_KEY_LEN] = 0;
	board_config->min_output_buffer_size =
		iniparser_getint(ini, ini_key, DEFAULT_MIN_OUTPUT_BUFFER_SIZE);

	iniparser_freedict(ini);
	syslog(LOG_DEBUG, "Loaded ini file %s", ini_name);
}


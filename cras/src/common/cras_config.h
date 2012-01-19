/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_CONFIG_H_
#define CRAS_CONFIG_H_

#define CRAS_MIN_BUFFER_SIZE_FRAMES 41

#define CRAS_SERVER_RT_THREAD_PRIORITY 12
#define CRAS_CLIENT_RT_THREAD_PRIORITY 10
#define CRAS_SOCKET_FILE ".cras_socket"
#define CRAS_AUD_FILE_PATTERN ".cras_aud"

/* Gets the path to the user's home directory. */
const char *cras_config_get_user_homedir();
/* Gets the path to save UDS socket files to. */
const char *cras_config_get_socket_file_dir();

#endif /* CRAS_CONFIG_H_ */

/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CRAS_SRC_SERVER_INIPARSER_WRAPPER_H_
#define CRAS_SRC_SERVER_INIPARSER_WRAPPER_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "iniparser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_INI_NAME_LENGTH 256
#define MAX_INI_KEY_LENGTH 63  // names like "output_source:output_0"

static inline dictionary* iniparser_load_wrapper(const char* ini_name) {
  struct stat st;
  int rc = stat(ini_name, &st);
  if (rc < 0) {
    return NULL;
  }
  return iniparser_load(ini_name);
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_INIPARSER_WRAPPER_H_

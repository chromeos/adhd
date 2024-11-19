// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SERVER_INI_INI_H_
#define CRAS_SERVER_INI_INI_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct CrasIniDict;

/**
 * Load the ini at the given path.
 * Returns NULL and logs on error.
 *
 * # Safety
 *
 * `ini_name` must be a NULL-terminated string.
 */
struct CrasIniDict *cras_ini_load(const char *ini_path);

/**
 * Free the dict.
 *
 * # Safety
 *
 * `dict` must be something returned from cras_ini_load().
 * Once a dict is freed it may not be used.
 */
void cras_ini_free(struct CrasIniDict *dict);

/**
 * Return the number of sections in this dict.
 *
 * # Safety
 *
 * `dict` must point to a dict that was returned from cras_ini_load().
 */
int cras_ini_getnsec(const struct CrasIniDict *dict);

/**
 * Return the name of the i-th section as a NULL-terminated string.
 *
 * # Safety
 *
 * `dict` must point to a dict that was returned from cras_ini_load().
 * The returned string is alive until dict is freed. Do not free it yourself.
 */
const char *cras_ini_getsecname(const struct CrasIniDict *dict, int i);

/**
 * Return the number of keys in the section.
 *
 * # Safety
 *
 * `dict` must point to a dict that was returned from cras_ini_load().
 */
int cras_ini_getsecnkeys(const struct CrasIniDict *dict, const char *section);

/**
 * Return the name of the i-th key in the section.
 *
 * # Safety
 *
 * `dict` must point to a dict that was returned from cras_ini_load().
 * The returned string is alive until dict is freed. Do not free it yourself.
 */
const char *cras_ini_getseckey(const struct CrasIniDict *dict, const char *section, int i);

/**
 * Get the value stored in dict. `section_and_key` is a string formatted as
 * `section_name:key_name`.
 * Returns `notfound` if not found.
 *
 * # Safety
 *
 * `dict` must point to a dict that was returned from cras_ini_load().
 * The returned string is alive until dict is freed. Do not free it yourself.
 */
const char *cras_ini_getstring(const struct CrasIniDict *dict,
                               const char *section_and_key,
                               const char *notfound);

/**
 * Get the value stored in dict. `section_and_key` is a string formatted as
 * `section_name:key_name`. The value is parsed with atoi.
 * Returns `notfound` if not found.
 *
 * # Safety
 *
 * `dict` must point to a dict that was returned from cras_ini_load().
 */
int cras_ini_getint(const struct CrasIniDict *dict, const char *section_and_key, int notfound);

#endif  /* CRAS_SERVER_INI_INI_H_ */

#ifdef __cplusplus
}
#endif

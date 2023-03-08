/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <syslog.h>

#include "cras/src/server/cras_volume_curve.h"
#include "cras/src/server/iniparser_wrapper.h"
#include "cras_util.h"
#include "third_party/utlist/utlist.h"

struct cras_card_config {
  dictionary* ini;
};

static struct cras_volume_curve* create_simple_step_curve(
    const struct cras_card_config* card_config,
    const char* control_name) {
  char ini_key[MAX_INI_KEY_LENGTH + 1];
  int max_volume;
  int volume_step;

  snprintf(ini_key, MAX_INI_KEY_LENGTH, "%s:max_volume", control_name);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  max_volume = iniparser_getint(card_config->ini, ini_key, 0);
  snprintf(ini_key, MAX_INI_KEY_LENGTH, "%s:volume_step", control_name);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  volume_step = iniparser_getint(card_config->ini, ini_key, 300);
  syslog(LOG_INFO, "Configure curve found for %s.", control_name);
  return cras_volume_curve_create_simple_step(max_volume, volume_step * 100);
}

static struct cras_volume_curve* create_explicit_curve(
    const struct cras_card_config* card_config,
    const char* control_name) {
  unsigned int i;
  char ini_key[MAX_INI_KEY_LENGTH + 1];
  long dB_values[101];

  for (i = 0; i < 101; i++) {
    snprintf(ini_key, MAX_INI_KEY_LENGTH, "%s:dB_at_%u", control_name, i);
    ini_key[MAX_INI_KEY_LENGTH] = 0;
    dB_values[i] = iniparser_getint(card_config->ini, ini_key, 0);
  }
  syslog(LOG_INFO, "Explicit volume curve found for %s.", control_name);
  return cras_volume_curve_create_explicit(dB_values);
}

static dictionary* load_card_config_ini(const char* config_path,
                                        const char* card_name,
                                        const char* extension) {
  char ini_name[MAX_INI_NAME_LENGTH + 1];
  snprintf(ini_name, sizeof(ini_name), "%s/%s%s", config_path, card_name,
           extension);

  dictionary* ini = iniparser_load_wrapper(ini_name);
  if (ini == NULL) {
    syslog(LOG_DEBUG, "No ini file %s", ini_name);
  } else {
    syslog(LOG_DEBUG, "Loaded ini file %s", ini_name);
  }

  return ini;
}

/*
 * Exported interface.
 */

struct cras_card_config* cras_card_config_create(const char* config_path,
                                                 const char* card_name) {
  struct cras_card_config* card_config = NULL;
  dictionary* ini;

  ini = load_card_config_ini(config_path, card_name, ".card_settings");
  if (ini == NULL) {
    // fall back to ini without .card_settings suffix
    ini = load_card_config_ini(config_path, card_name, "");
  }
  if (ini == NULL) {
    return NULL;
  }

  card_config = calloc(1, sizeof(*card_config));
  if (card_config == NULL) {
    syslog(LOG_ERR, "OOM creating card_config");
    iniparser_freedict(ini);
    return NULL;
  }

  card_config->ini = ini;
  return card_config;
}

void cras_card_config_destroy(struct cras_card_config* card_config) {
  assert(card_config);
  iniparser_freedict(card_config->ini);
  free(card_config);
}

struct cras_volume_curve* cras_card_config_get_volume_curve_for_control(
    const struct cras_card_config* card_config,
    const char* control_name) {
  char ini_key[MAX_INI_KEY_LENGTH + 1];
  const char* curve_type;

  if (card_config == NULL || control_name == NULL) {
    return NULL;
  }

  snprintf(ini_key, MAX_INI_KEY_LENGTH, "%s:volume_curve", control_name);
  ini_key[MAX_INI_KEY_LENGTH] = 0;
  curve_type = iniparser_getstring(card_config->ini, ini_key, NULL);

  if (curve_type && strcmp(curve_type, "simple_step") == 0) {
    return create_simple_step_curve(card_config, control_name);
  }
  if (curve_type && strcmp(curve_type, "explicit") == 0) {
    return create_explicit_curve(card_config, control_name);
  }
  syslog(LOG_DEBUG, "No configure curve found for %s.", control_name);
  return NULL;
}

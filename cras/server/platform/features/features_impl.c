/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/server/platform/features/features_impl.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>

#include "cras/server/main_message.h"
#include "cras/server/platform/features/features.h"
#include "cras/server/platform/features/features_backend.h"
#include "cras/server/platform/features/override.h"

#define DEFINE_FEATURE(name, default_enabled) [name] = {#name, default_enabled},
struct cras_feature features[NUM_FEATURES] = {
#include "cras/server/platform/features/features.inc"
};
#undef DEFINE_FEATURE

bool cras_feature_enabled(enum cras_feature_id id) {
  if (id >= NUM_FEATURES || id < 0) {
    syslog(LOG_ERR, "invalid feature ID: %d", id);
    return false;
  }
  if (features[id].overridden) {
    bool enabled = features[id].overridden_enabled;
    syslog(LOG_DEBUG, "feature %s overridden enabled = %d", features[id].name,
           enabled);
    return enabled;
  }
  bool enabled = cras_features_backend_get_enabled(id);
  syslog(LOG_DEBUG, "feature %s enabled = %d", features[id].name, enabled);
  return enabled;
}

void cras_features_set_override(enum cras_feature_id id, bool enabled) {
  features[id].overridden = true;
  features[id].overridden_enabled = enabled;
}

void cras_features_unset_override(enum cras_feature_id id) {
  features[id].overridden = false;
}

enum cras_feature_id cras_feature_get_id(const struct cras_feature* feature) {
  return feature - features;
}

static void notify_changed() {
  struct cras_main_message msg = {
      .length = sizeof(msg),
      .type = CRAS_MAIN_FEATURE_CHANGED,
  };
  cras_main_message_send(&msg);
}

void cras_features_init() {
  return cras_features_backend_init(notify_changed);
}

void cras_features_deinit() {
  return cras_features_backend_deinit();
}

enum cras_feature_id cras_feature_get_by_name(const char* name) {
  if (name == NULL) {
    return CrOSLateBootUnknown;
  }

  static_assert(
      CrOSLateBootUnknown == 0,
      "CrOSLateBootUnknown should be 0 as we start loop iteration at 1");
  for (int i = 1; i < NUM_FEATURES; i++) {
    if (strcmp(name, features[i].name) == 0) {
      return i;
    }
  }
  return CrOSLateBootUnknown;
}

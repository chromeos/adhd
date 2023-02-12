/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_FEATURES_OVERRIDE_H_
#define CRAS_FEATURES_OVERRIDE_H_

#include <stdbool.h>

#include "cras/src/server/cras_features.h"

// Override feature id enabled status.
void cras_features_set_override(enum cras_feature_id id, bool enabled);

// Unset feature id override.
void cras_features_unset_override(enum cras_feature_id id);

#endif

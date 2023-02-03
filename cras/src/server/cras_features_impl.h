/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_FEATURES_IMPL_H_
#define CRAS_FEATURES_IMPL_H_

#include <stdbool.h>

struct cras_feature {
	// The name of the feature, used when consulting featured.
	const char *const name;
	// Whether to enable the feature by default.
	const bool default_enabled;
};

bool cras_features_backend_get_enabled(const struct cras_feature *feature);

#endif

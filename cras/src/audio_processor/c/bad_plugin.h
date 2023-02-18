// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_AUDIO_PROCESSOR_C_BAD_PLUGIN_H_
#define CRAS_SRC_AUDIO_PROCESSOR_C_BAD_PLUGIN_H_

#include "plugin_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

enum status bad_plugin_oom_create(struct plugin_processor** out,
                                  const struct plugin_processor_config* config);

enum status bad_plugin_null_processor_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config);

enum status bad_plugin_null_ops_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config);

enum status bad_plugin_missing_run_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config);

enum status bad_plugin_missing_destroy_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config);

enum status bad_plugin_failing_run_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config);

#ifdef __cplusplus
}
#endif

#endif

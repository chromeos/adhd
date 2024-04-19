// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AUDIO_PROCESSOR_C_IGO_PLUGIN_H_
#define AUDIO_PROCESSOR_C_IGO_PLUGIN_H_

#include "plugin_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

enum status plugin_processor_create(
    struct plugin_processor** out,
    const struct plugin_processor_config* config);

#ifdef __cplusplus
}
#endif

#endif  // AUDIO_PROCESSOR_C_IGO_PLUGIN_H_

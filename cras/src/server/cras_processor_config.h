//
// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_SERVER_CRAS_PROCESSOR_CONFIG_H_
#define CRAS_SRC_SERVER_CRAS_PROCESSOR_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "cras/src/server/rust/include/cras_processor.h"

// Get the processor effect.
enum CrasProcessorEffect cras_processor_get_effect();

#ifdef __cplusplus
}
#endif

#endif

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_SERVER_CRAS_PROCESSOR_CONFIG_H_
#define CRAS_SRC_SERVER_CRAS_PROCESSOR_CONFIG_H_

#include <stdbool.h>

#include "cras/src/server/cras_iodev.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "cras/src/server/rust/include/cras_processor.h"

// Get the processor effect.
// `nc_provided_by_ap` indicates the availability of NC on AP.
// `effects` indicates the stream effects.
enum CrasProcessorEffect cras_processor_get_effect(
    bool nc_provided_by_ap,
    const struct cras_iodev* iodev,
    uint64_t effects);

#ifdef __cplusplus
}
#endif

#endif

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_ewma_power_reporter.h"

void cras_ewma_power_reporter_init() {}

void cras_ewma_power_reporter_set_enabled(bool enabled) {}

void cras_ewma_power_reporter_set_target(uint32_t stream_id) {}

void cras_ewma_power_reporter_streams_changed(
    struct cras_rstream* all_streams) {}

bool cras_ewma_power_reporter_should_calculate(const uint32_t stream_id) {
  return false;
}

int cras_ewma_power_reporter_report(const uint32_t stream_id,
                                    const struct ewma_power* ewma) {
  return 0;
}

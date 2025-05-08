# # Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Generated with devtools/rust_generate.py

WRITE_SOURCE_FILE_TARGETS = [
    "@//cras/common:generate_rust_common_h",
    "@//cras/server/ini:generate_ini_h",
    "@//cras/server/platform/dlc:generate_dlc_h",
    "@//cras/server/platform/features:generate_features_backend_h",
    "@//cras/server/processor:generate_processor_h",
    "@//cras/server/rate_estimator:generate_rate_estimator_h",
    "@//cras/server/s2:generate_s2_h",
    "@//cras/src/dsp/rust:generate_dsp.h",
]

#!/bin/bash
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source /usr/share/cros/init/cras-env.sh || exit 1

exec sudo -E -u cras \
        -- \
        /usr/bin/cras \
        ${DSP_CONFIG} ${DEVICE_CONFIG_DIR} \
        ${INTERNAL_UCM_SUFFIX} ${BOARD_NAME_CONFIG} ${CPU_MODEL_NAME_CONFIG} \
        ${CRAS_ARGS}

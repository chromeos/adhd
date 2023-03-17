# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Unified build config.
device_config_dir="$(cros_config /audio/main cras-config-dir)"
internal_ucm_suffix="$(cros_config /audio/main ucm-suffix)"

# Hardware information
# TODO(b/259385071): use cros_config /audio/main board
board_name="$(cros_config /arc/build-properties product)"
cpu_model_name="$( \
  cat /proc/cpuinfo \
  | grep -i 'model name' -m 1 \
  | sed 's/model name[ \t]*:[ \t]*//' \
  | sed 's/\ /_/g')"

device_config_dir="/etc/cras/${device_config_dir}"
DEVICE_CONFIG_DIR="--device_config_dir=${device_config_dir}"
DSP_CONFIG="--dsp_config=${device_config_dir}/dsp.ini"

if [ -n "${internal_ucm_suffix}" ]; then
  INTERNAL_UCM_SUFFIX="--internal_ucm_suffix=${internal_ucm_suffix}"
fi
if [ -n "${board_name}" ]; then
  BOARD_NAME_CONFIG="--board_name=${board_name}"
fi
if [ -n "${cpu_model_name}" ]; then
  CPU_MODEL_NAME_CONFIG="--cpu_model_name=${cpu_model_name}"
fi

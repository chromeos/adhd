# Copyright 2016 The ChromiumOS Authors
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
  | sed 's/model name[ \t]*:[ \t]*//')"

# Handle legacy config.
if [ -z "${device_config_dir}" ]; then
  # For boards that need a different device config, check which config
  # directory to use. Use that directory for both volume curves
  # and DSP config.
  if [ -f /etc/cras/get_device_config_dir ]; then
    device_config_dir="$(sh /etc/cras/get_device_config_dir)"
  fi
  if [ -f /etc/cras/get_internal_ucm_suffix ]; then
    internal_ucm_suffix="$(sh /etc/cras/get_internal_ucm_suffix)"
  fi
else
  device_config_dir="/etc/cras/${device_config_dir}"
fi

if [ -n "${device_config_dir}" ]; then
  DEVICE_CONFIG_DIR="--device_config_dir=${device_config_dir}"
  DSP_CONFIG="--dsp_config=${device_config_dir}/dsp.ini"
fi
if [ -n "${internal_ucm_suffix}" ]; then
  INTERNAL_UCM_SUFFIX="--internal_ucm_suffix=${internal_ucm_suffix}"
fi
if [ -n "${board_name}" ]; then
  BOARD_NAME_CONFIG="--board_name=${board_name}"
fi
if [ -n "${cpu_model_name}" ]; then
  CPU_MODEL_NAME_CONFIG="--cpu_model_name=${cpu_model_name}"
fi

# Leave cras in the init pid namespace as it uses its PID as an IPC identifier.
exec minijail0 -u cras -g cras -G --uts -v -l \
        -T static \
        -P /mnt/empty \
        -b /,/ \
        -k 'tmpfs,/run,tmpfs,MS_NODEV|MS_NOEXEC|MS_NOSUID,mode=755,size=10M' \
        -b /run/bluetooth/audio,/run/bluetooth/audio \
        -b /run/cras,/run/cras,1 \
        -b /run/dbus,/run/dbus,1 \
        -k '/run/imageloader,/run/imageloader,none,MS_BIND|MS_REC' \
        -b /run/udev,/run/udev \
        -b /dev,/dev \
        -b /dev/shm,/dev/shm,1 \
        -k proc,/proc,proc \
        -b /sys,/sys \
        -k 'tmpfs,/var,tmpfs,MS_NODEV|MS_NOEXEC|MS_NOSUID,mode=755,size=10M' \
        -b /var/lib/metrics/,/var/lib/metrics/,1 \
        -- \
        /sbin/minijail0 -n \
        -S /usr/share/policy/cras-seccomp.policy \
        -- \
        /usr/bin/cras \
        ${DSP_CONFIG} ${DEVICE_CONFIG_DIR} \
        ${INTERNAL_UCM_SUFFIX} ${BOARD_NAME_CONFIG} ${CPU_MODEL_NAME_CONFIG} \
        ${CRAS_ARGS}

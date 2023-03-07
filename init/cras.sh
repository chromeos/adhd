# Copyright 2016 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source /usr/share/cros/init/cras-env.sh || exit 1
# Leave cras in the init pid namespace as it uses its PID as an IPC identifier.
exec minijail0 -u cras -g cras -G --uts -v -l \
        -T static \
        -P /mnt/empty \
        -b /,/ \
        -b '/usr/share/alsa/ucm/Loopback/HiFi.conf,/usr/share/alsa/ucm/Loopback/HiFi.conf' \
        -f /sys/fs/cgroup/cpuset/user_space/media/tasks \
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

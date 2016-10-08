# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# For Samus only, check which dsp.ini to load.
if [ "$(mosys platform name)" = "Samus" ]; then
  hw_version="$(mosys platform version)"
  if [ "$hw_version" = "MP.A" ] ||
     [ "$hw_version" = "EVT" ] ||
     [ "$hw_version" = "DVT" ] ||
     [ "$hw_version" = "PVT" ]; then
       DSP_CONFIG="--dsp_config=/etc/cras/dsp.samus.orig.ini"
  fi
fi
# For board needs different device configs, check which config
# directory to use. Use that directory for both volume curves
# and dsp config.
if [ -f /etc/cras/get_device_config_dir ]; then
  device_config_dir="$(sh /etc/cras/get_device_config_dir)"
  DEVICE_CONFIG_DIR="--device_config_dir=${device_config_dir}"
  DSP_CONFIG="--dsp_config=${device_config_dir}/dsp.ini"
fi
exec minijail0 -i -u cras -g cras -G -- /usr/bin/cras \
    ${DSP_CONFIG} ${DEVICE_CONFIG_DIR}

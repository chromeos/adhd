#!/usr/bin/env python3
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Print beamforming enum variants for Chromium's tools/metrics/histograms/metadata/cras/histograms.xml
"""

import configparser
import glob
import pathlib


overlays_dir = pathlib.Path(__file__).absolute().parents[3] / 'overlays'

dlcs = {}

for fn in overlays_dir.glob('**/board.ini'):
    cp = configparser.ConfigParser()
    cp.read(fn)
    dlc_id = cp.get('cras_processor_vars', 'beamforming_dlc_id', fallback=None)
    if dlc_id is None:
        continue
    model = pathlib.Path(fn).parents[2].name
    dlcs.setdefault(dlc_id, set()).add(model)

for dlc_id, models in sorted(dlcs.items()):
    modelss = ', '.join(sorted(models))
    print(f'''<variant name="{dlc_id}" summary="the intelligo beamforming DLC for {modelss}"/>''')

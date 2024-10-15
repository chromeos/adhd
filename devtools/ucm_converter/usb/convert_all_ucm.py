#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import sys

from ucm_to_audio_config import dict_to_xml
from ucm_to_audio_config import ucm_to_dict
from ucm_to_mixer import generate_mixer_paths_xml
from ucm_to_mixer import parse_ucm_config


def convert_ucm_configs(base_dir, output_dir, operation):
    """Converts USB UCM config files to mixer_paths.xml or audio_config.xml depending on operation."""

    for device_dir in os.listdir(base_dir):
        device_path = os.path.join(base_dir, device_dir)
        canonical_device_dir = device_dir.translate(str.maketrans(" ()&", "_[]+"))

        if os.path.isdir(device_path):
            ucm_file = os.path.join(device_path, "HiFi.conf")

            if os.path.isfile(ucm_file):
                with open(ucm_file, "r") as f:
                    ucm_string = f.read()
                    if re.search(r"FullySpecifiedUCM", ucm_string) is None:
                        print(f"Skipped non-fully-specified UCM {ucm_file}")
                        continue

                device_output_dir = os.path.join(output_dir, canonical_device_dir)
                os.makedirs(device_output_dir, exist_ok=True)
                if operation == "mixer":
                    output_file = os.path.join(device_output_dir, "mixer_paths.xml")
                    config_data = parse_ucm_config(ucm_file)
                    generate_mixer_paths_xml(config_data, output_file)
                elif operation == "xml":
                    output_file = os.path.join(device_output_dir, "audio_config.xml")
                    data_dict = ucm_to_dict(ucm_file)
                    dict_to_xml(data_dict, output_file)
                else:
                    print(f"Invalid operation: {operation}")
                    continue


if __name__ == "__main__":
    # Argument parsing using argparse
    parser = argparse.ArgumentParser(
        description="Convert USB UCM config files to mixer_paths.xml or audio_config.xml"
    )
    parser.add_argument(
        "base_dir",
        help="${CHROMIUMOS}/src/third_party/adhd/ucm-config/for_all_boards",
    )
    parser.add_argument(
        "output_dir", help="Path to the output directory where converted files will be saved"
    )
    parser.add_argument(
        "operation", choices=["mixer", "xml"], help="Conversion operation: 'mixer' or 'xml'"
    )
    args = parser.parse_args()

    convert_ucm_configs(args.base_dir, args.output_dir, args.operation)

#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

from ucm_to_audio_config import dict_to_xml
from ucm_to_audio_config import ucm_to_dict
from ucm_to_mixer import generate_mixer_paths_xml
from ucm_to_mixer import parse_ucm_config


def get_card_name(ucm_file):
    with open(ucm_file, "r") as f:
        ucm_string = f.read()

    current_device = None
    lines = ucm_string.strip().split("\n")

    # Extract SoundCardName from the first occurrence of cdev
    for line in lines:
        line = line.strip()
        if line.startswith("cdev"):
            return line.split(" ")[1].strip('"').split('hw:')[1]


def convert_ucm_configs(base_dir, output_dir, operation):
    """Converts UCM config files to mixer_paths.xml or audio_config.xml depending on operation."""

    audio_ucm_path = os.path.join("audio", "ucm-config")

    for model_dir in os.listdir(base_dir):
        model_path = os.path.join(base_dir, model_dir)

        if os.path.isdir(model_path) and os.path.isdir(os.path.join(model_path, audio_ucm_path)):
            for ucm_suffix in os.listdir(os.path.join(model_path, audio_ucm_path)):
                ucm_dir = os.path.join(model_path, audio_ucm_path, ucm_suffix)
                ucm_file = os.path.join(ucm_dir, "HiFi.conf")

                if os.path.isfile(ucm_file):
                    card_name = get_card_name(ucm_file)
                    model_output_dir = os.path.join(
                        output_dir, model_dir, f"{card_name}.{model_dir}"
                    )
                    os.makedirs(model_output_dir, exist_ok=True)
                    if operation == "mixer":
                        output_file = os.path.join(model_output_dir, "mixer_paths.xml")
                        config_data = parse_ucm_config(ucm_file)
                        generate_mixer_paths_xml(config_data, output_file)
                    elif operation == "xml":
                        output_file = os.path.join(model_output_dir, "audio_config.xml")
                        data_dict = ucm_to_dict(ucm_file)
                        dict_to_xml(data_dict, output_file)
                    else:
                        print(f"Invalid operation: {operation}")
                        continue


if __name__ == "__main__":
    # Argument parsing using argparse
    parser = argparse.ArgumentParser(
        description="Convert UCM config files to mixer_paths.xml or audio_config.xml"
    )
    parser.add_argument(
        "base_dir",
        help="${CHROMIUMOS}/src/overlays/overlay-${BOARD}/chromeos-base/chromeos-bsp-${BOARD}/files",
    )
    parser.add_argument(
        "output_dir", help="Path to the output directory where converted files will be saved"
    )
    parser.add_argument(
        "operation", choices=["mixer", "xml"], help="Conversion operation: 'mixer' or 'xml'"
    )
    args = parser.parse_args()

    convert_ucm_configs(args.base_dir, args.output_dir, args.operation)

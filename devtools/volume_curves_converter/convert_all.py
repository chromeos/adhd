# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re

from ini_to_xml import convert


def get_alphanumeric(s):
    return re.sub(r'[^a-zA-Z0-9]', '', s)


def convert_all(base_dir, output_dir, unknown_card_output_dir):
    for model_dir in os.listdir(base_dir):
        if not os.path.isdir(os.path.join(output_dir, model_dir)):
            print(f"UCM config for {model_dir} is not found. Skipping")
            continue

        model_path = os.path.join(base_dir, model_dir)
        cras_config_path = os.path.join(model_path, "audio", "cras-config")

        if os.path.isdir(cras_config_path):
            for card_settings in os.listdir(cras_config_path):
                if card_settings.endswith(".ini"):
                    continue

                input_file = os.path.join(cras_config_path, card_settings)
                if os.path.isdir(input_file):
                    print("Found a directory inside. Model:", model_path)
                    continue

                if card_settings == "card_settings":
                    # Get the card_name from the ucm_converter output
                    entries = os.listdir(os.path.join(output_dir, model_dir))
                    if len(entries) == 1:
                        card_name = entries[0].split('.')[0]
                    else:
                        print("Found multiple output dirs when input_file = card_settings")
                        card_name = card_settings

                elif card_settings.endswith(".card_settings"):
                    card_name = get_alphanumeric(card_settings.replace(".card_settings", ""))

                else:
                    card_name = get_alphanumeric(card_settings)

                # Some card_names are truncated.
                found_output_dir = False
                for truncate in range(len(card_name)):
                    card_output_dir = os.path.join(
                        model_dir, f"{card_name[:len(card_name)-truncate]}.{model_dir}"
                    )
                    if os.path.isdir(os.path.join(output_dir, card_output_dir)):
                        card_output_dir = os.path.join(output_dir, card_output_dir)
                        found_output_dir = True
                        break

                if not found_output_dir:
                    print(
                        f"The output directory for model: {model_dir}, card: {card_name} is not found"
                    )
                    card_output_dir = os.path.join(model_dir, f"{card_name}.{model_dir}")
                    card_output_dir = os.path.join(unknown_card_output_dir, card_output_dir)
                    os.makedirs(card_output_dir, exist_ok=False)

                output_file = os.path.join(card_output_dir, "volume_curves.xml")
                convert(input_file, output_file)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Convert volume curves from card_settings to stream_volumes.xml"
    )
    parser.add_argument(
        "base_dir",
        help="${CHROMIUMOS}/src/overlays/overlay-${BOARD}/chromeos-base/chromeos-bsp-${BOARD}/files",
    )
    parser.add_argument(
        "output_dir",
        help="Path to the output directory where converted files will be saved. "
        + "It should be the same with the path that is used by ucm_converter script.",
    )
    parser.add_argument(
        "unknown_card_output_dir",
        help="Directory for converted files if the directory created "
        + "by ucm_converter is not found.",
    )
    args = parser.parse_args()

    convert_all(args.base_dir, args.output_dir, args.unknown_card_output_dir)

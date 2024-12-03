#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import xml.etree.ElementTree as ET

import yaml


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


def list_to_xml_element(data, root, key):
    """Converts a array to XML elements."""
    for item in data:
        if isinstance(item, dict):
            sub_element = ET.SubElement(root, key)
            dict_to_xml_element(item, sub_element)
        elif isinstance(item, list):
            sub_element = ET.SubElement(root, key)
            list_to_xml_element(item, sub_element, key)
        else:
            ET.SubElement(root, key).text = str(item)


def dict_to_xml_element(data, root):
    """Converts a dictionary to XML elements."""
    for key, value in data.items():
        if isinstance(value, dict):
            sub_element = ET.SubElement(root, key)
            dict_to_xml_element(value, sub_element)
        elif isinstance(value, list):
            list_to_xml_element(value, root, key)
        else:
            ET.SubElement(root, key).text = str(value)


def dict_to_xml(data, output_file):
    root = ET.Element("sound_card_init")
    dict_to_xml_element(data, root)
    # add the licence comment in the xml
    comment = ET.Comment(
        "\nCopyright (C) 2025 The Android Open Source Project\n\n"
        "Licensed under the Apache License, Version 2.0 (the \"License\");\n"
        "you may not use this file except in compliance with the License.\n"
        "You may obtain a copy of the License at\n\n"
        "     http://www.apache.org/licenses/LICENSE-2.0\n\n"
        "Unless required by applicable law or agreed to in writing, software\n"
        "distributed under the License is distributed on an \"AS IS\" BASIS,\n"
        "WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n"
        "See the License for the specific language governing permissions and\n"
        "limitations under the License.\n"
    )
    root.insert(0, comment)  # insert before the root's first child
    tree = ET.ElementTree(root)
    ET.indent(tree, space="\t", level=0)

    tree.write(output_file, encoding="utf-8", xml_declaration=True)

    print(f"XML written to {output_file}")


def parse_sound_card_init_yaml(config):
    file = os.open(config, os.O_RDONLY)
    with open(config) as file:
        yaml_data = yaml.load(file, Loader=yaml.SafeLoader)
        return yaml_data


def get_amp_name(yaml_file):
    return yaml_file.split('.')[1]


def convert_sound_card_init_configs(base_dir, output_dir):
    """Converts sound-card-init-config yaml files to AMP.xml."""

    audio_sound_card_init_path = os.path.join("audio", "sound-card-init-config")
    audio_ucm_path = os.path.join("audio", "ucm-config")

    for model_dir in os.listdir(base_dir):
        model_path = os.path.join(base_dir, model_dir)

        if os.path.isdir(model_path) and os.path.isdir(
            os.path.join(model_path, audio_sound_card_init_path)
        ):
            for yaml_file_name in os.listdir(os.path.join(model_path, audio_sound_card_init_path)):
                config = os.path.join(model_path, audio_sound_card_init_path, yaml_file_name)

                if os.path.isfile(config):
                    amp_name = get_amp_name(yaml_file_name)
                    for ucm_suffix in os.listdir(os.path.join(model_path, audio_ucm_path)):
                        ucm_dir = os.path.join(model_path, audio_ucm_path, ucm_suffix)
                        ucm_file = os.path.join(ucm_dir, "HiFi.conf")
                        if os.path.isfile(ucm_file):
                            card_name = get_card_name(ucm_file)
                            model_output_dir = os.path.join(
                                output_dir, model_dir, f"{card_name}.{model_dir}"
                            )

                            output_file = os.path.join(model_output_dir, f"{amp_name}.xml")
                            # Create the directory for the output file if it doesn't exist
                            os.makedirs(os.path.dirname(output_file), exist_ok=True)
                            config_data = parse_sound_card_init_yaml(config)
                            dict_to_xml(config_data, output_file)


if __name__ == "__main__":
    # Argument parsing using argparse
    parser = argparse.ArgumentParser(
        description="Convert sound-card-init-config yaml files to AMP.xml"
    )
    parser.add_argument(
        "base_dir",
        help="${CHROMIUMOS}/src/overlays/overlay-${BOARD}/chromeos-base/chromeos-bsp-${BOARD}/files",
    )
    parser.add_argument(
        "output_dir", help="Path to the output directory where converted files will be saved"
    )

    args = parser.parse_args()

    convert_sound_card_init_configs(args.base_dir, args.output_dir)

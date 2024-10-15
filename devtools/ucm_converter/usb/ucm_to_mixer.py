#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET


def normalized_control_value(value):
    if value == "on":
        return "1"
    if value == "off":
        return "0"
    return value


def parse_ucm_config(ucm_file):
    """Parses the UCM config file and extracts relevant sections."""
    config_data = {}
    sum_disable_sequence = set()
    with open(ucm_file, 'r') as file:
        current_section = None
        current_sequence = None  # Track the current sequence (Enable/Disable)

        for line in file:
            line = line.strip()
            # Section headers
            if line.startswith("Section"):
                current_section = line.strip(" {")
                config_data[current_section] = {"EnableSequence": [], "DisableSequence": []}

            # Sequence headers
            elif line.startswith("EnableSequence"):
                current_sequence = "EnableSequence"
            elif line.startswith("DisableSequence"):
                current_sequence = "DisableSequence"

            # Skip empty lines and comments
            elif not line or line.startswith("//"):
                continue

            # Extract cset/cset-tlv commands
            if line.startswith("cset"):
                match = re.search(r"name='(.*?)'\s*,?\s*(.*)\"", line)
                if match:
                    key, value = match.groups()
                    value = normalized_control_value(value)
                    config_data[current_section][current_sequence].append((key, value))
                    if current_sequence == "DisableSequence":
                        sum_disable_sequence.add((key, value))

    for key in config_data.keys():
        seq_set = sum_disable_sequence.copy()
        if "DisableSequence" in config_data[key]:
            seq_set -= set(config_data[key]["DisableSequence"])
        if len(seq_set) > 0:
            config_data[key]["EnableSequence"] = [*seq_set] + config_data[key]["EnableSequence"]

    return config_data


def generate_card_initial_setting(config_data):
    initial_setting = {}

    def check_dup_key_in_initial_setting(key, value):
        if key in initial_setting and value != initial_setting[key]:
            print(
                f"Error: overwrite initial_setting: key '{key}'. new value '{value}', previous value:'{initial_setting[key]}'"
            )
        else:
            initial_setting[key] = value

    # Add initial settings from SectionVerb EnableSequence
    verb_settings = config_data.get("SectionVerb", {}).get("EnableSequence", [])
    for key, value in verb_settings:
        check_dup_key_in_initial_setting(key, value)

    # Add initial settings from SectionDevice DisableSequence
    for section_name, data in config_data.items():
        if section_name.startswith("SectionDevice") or section_name.startswith("SectionModifier"):
            for key, value in data.get("DisableSequence", []):
                check_dup_key_in_initial_setting(key, value)
    return initial_setting


def generate_mixer_paths_xml(config_data, output_file):
    """Generates the mixer_paths.xml content using ElementTree and saves it to the output file."""
    # Create root element
    mixer = ET.Element("mixer")
    mixer.set("enum_mixer_numeric_fallback", "true")

    initial_setting = generate_card_initial_setting(config_data)
    for key, value in initial_setting.items():
        ET.SubElement(mixer, "ctl", name=key, value=value)

    # Process devices and modifiers (EnableSequence)
    for section_name, data in config_data.items():
        if section_name.startswith("SectionDevice") or section_name.startswith("SectionModifier"):
            device_type = re.search(r'"([^"]*)"', section_name).group(1).replace(" ", "-").lower()
            path = ET.SubElement(mixer, "path", name=device_type)
            for key, value in data.get("EnableSequence", []):
                ET.SubElement(path, "ctl", name=key, value=value)

    # Add the licence comment in the xml
    comment = ET.Comment(
        "Copyright 2024 The Chromium OS Authors. All rights reserved.\n"
        "Use of this source code is governed by a BSD-style license that can be\n"
        "found in the LICENSE file."
    )
    mixer.insert(0, comment)  # insert before the root's first child
    tree = ET.ElementTree(mixer)
    ET.indent(tree, space="\t", level=0)  # For better readability
    tree.write(output_file, encoding="utf-8", xml_declaration=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert UCM config to mixer_paths.xml")
    parser.add_argument("input_ucm_file", help="Path to the input UCM config file")
    parser.add_argument("output_file", help="Path to the output mixer_paths.xml file")
    args = parser.parse_args()

    config_data = parse_ucm_config(args.input_ucm_file)

    # Create output directory if it doesn't exist
    output_dir = os.path.dirname(args.output_file)
    os.makedirs(output_dir, exist_ok=True)

    generate_mixer_paths_xml(config_data, args.output_file)

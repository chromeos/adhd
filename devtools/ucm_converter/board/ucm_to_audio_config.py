#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys
import xml.etree.ElementTree as ET


# Map of keys to extract and their XML tags, in alphabetical order
KEY_MAP = {
    "CaptureChannels": "CaptureChannels",
    "CaptureChannelMap": "CaptureChannelMap",
    "CaptureMixerElem": "CaptureMixerElem",
    "CapturePCM": "CapturePCM",
    "CaptureRate": "CaptureRate",
    "EDIDFile": "EDIDFile",
    "IntrinsicSensitivity": "IntrinsicSensitivity",
    "JackDev": "JackDev",
    "JackSwitch": "JackSwitch",
    "PlaybackChannels": "PlaybackChannels",
    "PlaybackMixerElem": "PlaybackMixerElem",
    "PlaybackPCM": "PlaybackPCM",
    "PlaybackRate": "PlaybackRate",
}


# Special handler function for CaptureChannelMap
def handle_channel_map(channel_map_str):
    """Converts a CaptureChannelMap string to a dictionary,
    excluding entries with value '-1'."""
    channel_values = channel_map_str.split(" ")
    channel_names = ["FL", "FR", "RL", "RR", "FC", "LFE", "SL", "SR", "RC", "FLC", "FRC"]
    channel_map = dict(zip(channel_names, channel_values))
    return {k: v for k, v in channel_map.items() if v != "-1"}


# Map of special keys to their handling functions
SPECIAL_KEY_HANDLERS = {
    "CaptureChannelMap": handle_channel_map,
    # Add more special key handlers here if needed
}


def ucm_to_dict(input_file):
    """Converts a UCM string to a Python dictionary."""
    with open(input_file, "r") as f:
        ucm_string = f.read()

    data = {}
    current_device = None
    lines = ucm_string.strip().split("\n")

    # Extract SoundCardName from the first occurrence of cdev
    for line in lines:
        line = line.strip()
        if line.startswith("cdev"):
            data["SoundCardName"] = line.split(" ")[1].strip('"')
            break  # Exit loop after extracting SoundCardName

    data['HiFi'] = {}
    for line in lines:
        line = line.strip()
        if line.startswith(("EnableSequence", "DisableSequence", "cset")):
            continue

        for key, xml_tag in KEY_MAP.items():
            if line.startswith(key):
                value = line.split(key)[1].strip("\" ")
                if current_device:
                    handler = SPECIAL_KEY_HANDLERS.get(key, lambda x: x)
                    data['HiFi'][current_device][xml_tag] = handler(value)
                break

        if line.startswith("SectionDevice"):
            current_device = line.split(".")[1].strip(' ".0').replace(" ", "")
            data['HiFi'][current_device] = {}
    return data


def dict_to_xml(data, output_file):
    root = ET.Element("audio_config")
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

    with open(output_file, "wb") as f:
        tree.write(f, encoding="utf-8", xml_declaration=True)

    print(f"XML written to {output_file}")


def dict_to_xml_element(data, root):
    """Converts a dictionary to XML elements."""
    for key, value in data.items():
        if isinstance(value, dict):
            sub_element = ET.SubElement(root, key)
            dict_to_xml_element(value, sub_element)
        else:
            ET.SubElement(root, key).text = value


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert UCM to XML.")
    parser.add_argument("input_file", help="Path to the input UCM file")
    parser.add_argument("output_file", help="Path to the output XML file")
    args = parser.parse_args()

    # Create the directory for the output file if it doesn't exist
    output_dir = os.path.dirname(args.output_file)
    os.makedirs(output_dir, exist_ok=True)

    data_dict = ucm_to_dict(args.input_file)
    dict_to_xml(data_dict, args.output_file)

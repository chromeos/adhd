#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET


# Map of keys that affect all output device sections and their XML tags
GLOBAL_OUTPUT_KEY_MAP = {
    "UseSoftwareVolume": "UseSoftwareVolume",
}

# Map of keys to extract and their XML tags, in alphabetical order
KEY_MAP = {
    "CaptureChannelMap": "CaptureChannelMap",
    "CaptureChannels": "CaptureChannels",
    "CaptureMixerElem": "CaptureMixerElem",
    "CapturePCM": "CapturePCM",
    "CaptureRate": "CaptureRate",
    "EDIDFile": "EDIDFile",
    "IntrinsicSensitivity": "IntrinsicSensitivity",
    "JackDev": "JackDev",
    "JackSwitch": "JackSwitch",
    "PlaybackChannelMap": "PlaybackChannelMap",
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
    "PlaybackChannelMap": handle_channel_map,
    # Add more special key handlers here if needed
}


def ucm_to_dict(input_file):
    """Converts a UCM string to a Python dictionary."""
    with open(input_file, "r") as f:
        ucm_string = f.read()

    data = {}
    current_device = None
    lines = ucm_string.strip().split("\n")

    # Extract SoundCardName from the first occurrence of hw:
    for line in lines:
        match = re.search(r'(hw:[^,]+),\d+', line)
        if match:
            section_name = match.group(1)
            data["SoundCardName"] = section_name
            break  # Exit loop after extracting SoundCardName

    # These are special keys that could affect all output sections
    global_output_tags = {}
    for line in lines:
        line = line.strip()
        for key, xml_tag in GLOBAL_OUTPUT_KEY_MAP.items():
            if line.startswith(key):
                global_output_tags[key] = line.split(" ")[1].strip('"')

    # Append a filler so the last section gets pushed into the data
    lines.append('SectionDevice."filler".0')

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
                    current_device[xml_tag] = handler(value)
                break

        if line.startswith("SectionDevice"):
            if current_device is not None:
                if 'PlaybackPCM' in current_device:
                    for key, value in global_output_tags.items():
                        xml_tag = GLOBAL_OUTPUT_KEY_MAP[key]
                        current_device[xml_tag] = value
                    data['HiFi']['USBOut'] = data['HiFi'].get('USBOut', [])
                    data['HiFi']['USBOut'].append({'Section': current_device})
                elif 'CapturePCM' in current_device:
                    data['HiFi']['USBIn'] = data['HiFi'].get('USBIn', [])
                    data['HiFi']['USBIn'].append({'Section': current_device})
                else:
                    name = current_device['SectionName']
                    print(f"Cannot determine direction of {name} for {input_file}")

            section_name = re.search(r'"([^"]*)"', line).group(1)
            current_device = {'SectionName': section_name}

    return data


def dict_to_xml(data, output_file):
    root = ET.Element("audio_config")
    dict_to_xml_element(data, root)
    # add the licence comment in the xml
    comment = ET.Comment(
        "Copyright 2024 The Chromium OS Authors. All rights reserved.\n"
        "Use of this source code is governed by a BSD-style license that can be\n"
        "found in the LICENSE file."
    )
    root.insert(0, comment)  # insert before the root's first child
    tree = ET.ElementTree(root)
    ET.indent(tree, space="\t", level=0)

    with open(output_file, "wb") as f:
        tree.write(f, encoding="utf-8", xml_declaration=True)


def dict_to_xml_element(data, root):
    """Converts a dictionary to XML elements."""
    for key, value in data.items():
        sub_element = ET.SubElement(root, key)
        if isinstance(value, dict):
            dict_to_xml_element(value, sub_element)
        elif isinstance(value, list):
            for item in value:
                dict_to_xml_element(item, sub_element)
        else:
            sub_element.text = value


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

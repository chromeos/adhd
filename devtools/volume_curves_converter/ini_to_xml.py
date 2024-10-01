# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import configparser
import xml.etree.ElementTree as ET


# Read the file and remove indentation
def preprocess_ini(file_path):
    with open(file_path, 'r') as file:
        lines = file.readlines()

    # Remove indentation for lines that start with spaces
    processed_lines = [line.lstrip() for line in lines]

    return '\n'.join(processed_lines)


def load_config(file_path):
    config = configparser.ConfigParser()
    file_str = preprocess_ini(file_path)
    config.read_string(file_str)

    return config


def read_cras_volume_curve(input_file):
    """
    Returns a dictionary with this structure
    {
        device_type: [db_at_0, db_at_1, ..., db_at_100]
    }
    """

    def save_simple_step(data, device, cras_curve):
        step = int(cras_curve[device]["volume_step"])
        max = int(cras_curve[device]["max_volume"])

        vol = [0] * 101
        for db in range(100, -1, -1):
            vol[db] = max
            max -= step

        data[device] = vol

    def save_explicit(data, device, cras_curve):
        data[device] = [int(cras_curve[device][f'db_at_{i}']) for i in range(101)]

    data = {}
    cras_curve = load_config(input_file)
    for device in cras_curve.sections():
        try:
            curve_type = cras_curve[device]["volume_curve"]
            if curve_type == "explicit":
                save_explicit(data, device, cras_curve)

            elif curve_type == "simple_step":
                save_simple_step(data, device, cras_curve)

        except Exception as e:
            print(f"Exception found while reading: {input_file}, e:{e}", input_file, e)

    return data


def dict_to_xml_element(data, root):
    for key, value in data.items():
        sub_element = ET.SubElement(root, "reference", name=key)
        for idx, vol in enumerate(value):
            ET.SubElement(sub_element, "point").text = f"{idx},{vol}"


def dict_to_xml(data, output_file):
    """
    Writes an xml with following format
    <?xml version='1.0' encoding='utf-8'?>
    <volumes>
        <reference name="Speaker">
            <point>0,-6400</point>
            ...
            <point>100,-170</point>
        </reference>
        <reference name="Headphone">
            <point>0,-6600</point>
            ...
            <point>100,0</point>
        </reference>
    </volumes>
    """
    root = ET.Element("volumes")

    comment = ET.Comment(
        "Copyright 2024 The Chromium OS Authors. All rights reserved.\n"
        "Use of this source code is governed by a BSD-style license that can be\n"
        "found in the LICENSE file."
    )
    root.insert(0, comment)

    dict_to_xml_element(data, root)
    tree = ET.ElementTree(root)

    ET.indent(tree, space=4 * ' ', level=0)
    with open(output_file, "wb") as f:
        tree.write(f, encoding="utf-8", xml_declaration=True)
        f.write(b'\n')

    print(f"XML written to {output_file}")


def convert(input_file, output_file):
    data = read_cras_volume_curve(input_file)
    dict_to_xml(data, output_file)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert cras volume curve to android xml format")
    parser.add_argument("input_file", help="Path to cras volume curve")
    parser.add_argument("output_file", help="Path to the output xml file")
    args = parser.parse_args()

    convert(args.input_file, args.output_file)

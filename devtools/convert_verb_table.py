#!/usr/bin/env vpython3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This scripts converts verb tables provided by vendors to coreboot supported format.

Currently only supports verb tables provided by Realtek in typical format.
Please also remember to check the output to align with comments and other small
errors.
"""

import argparse
import string


def decompose_verb(verb_command):
    """
    Verb commands in 12-bit format are formatted as the following:

        |---------------|---------|---------|---------|
        | Codec Address | Node ID | Verb ID | Payload |
        |---------------|---------|---------|---------|
        |     4 bits    | 8 bits  | 12 bits | 8 bits  |

    specified in HD-Audio specification.

    decompose_verb mask out each part of the verb.
    """
    codec = verb_command >> 28
    pin = (verb_command & 0xFF00000) >> 20
    verb = (verb_command & 0xFFF00) >> 8
    val = verb_command & 0xFF
    return codec, pin, verb, val


def match_and_replace(verbs):
    """Try to replace verbs groups with azalia functions.

    Args:
        verbs: list of 4 verb commands in str.

    Returns:
        A list consist of either azalia functions or 4 verbs.
    """
    if len(verbs) != 4:
        return verbs

    # The macros are defined in coreboot: src/include/device/azalia_device.h.
    # Verb IDs are predefined in those macros.
    pin_cfg = [0x71C, 0x71D, 0x71E, 0x71F]
    reset = [0x7FF, 0x7FF, 0x7FF, 0x7FF]
    subvendor = [0x720, 0x721, 0x722, 0x723]

    # Make sure the order of macros and outputs match respectively
    macros = [pin_cfg, reset, subvendor]
    outputs = [
        "AZALIA_PIN_CFG({common_codec}, 0x{common_pin:02x}, 0x{combined_val:02x}),",
        "AZALIA_RESET(0x{common_pin:02x}),",
        "AZALIA_SUBVENDOR({common_codec}, 0x{combined_val:02x}),",
    ]

    common_codec, common_pin, verb, val = decompose_verb(int(verbs[0], 16))

    for macro, output in zip(macros, outputs):
        combined_val = 0
        shift = 0
        for match_verb, verb_command in zip(macro, verbs):
            codec, pin, verb, val = decompose_verb(int(verb_command, 16))
            if codec != common_codec or pin != common_pin or verb != match_verb:
                break
            combined_val = combined_val | (val << shift)
            shift += 8
        else:
            return [
                output.format(
                    combined_val=combined_val, common_pin=common_pin, common_codec=common_codec
                )
            ]

    return verbs


def parse(filename):
    """Parse a verb table and split it into groups of 4 verbs.

    Args:
        filename: file that consist of the verb table.

    Returns:
        A dict that contains:
            verbs: a list of lists that contain 4 verbs/azalia function with
                   associated comment.
            beep: a list of lists that contain 4 verbs/azalia function with
                 associated comment, provided for beep function.
            ssid: HDA subsystem ID.
            count: number of verbs in verb table divided by 4.
    Raises:
        ValueError: the number of verbs in verb table is not divisible by 4.
    """
    count_of_groups = 0  # Number of verb command block (4 verb)
    current_title = ""
    current_group = []
    verbs = []
    beep_verbs = []
    beep = False  # The rest is beep_verbs
    ssid = None
    with open(filename, "r") as input_file:
        for line in input_file:
            if line.startswith(";"):  # Comments
                if line.startswith(";HDA Codec Subsystem ID"):
                    ssid = line.split()[-1]
                current_title = "/* " + line[1:].strip() + " */"
                if "BEEP" in line and "Pin widget" not in line and not beep:
                    beep = True

            elif line.startswith("dd"):  # Verbs, formed with dd xxxxxxxxh
                verb = line.split()[-1]
                verb = "0x" + verb[0:-1]
                current_group.append(verb)

            if len(current_group) == 4:
                if "DMIC" in current_title:
                    current_group = []
                    current_title = ""
                    continue

                if beep:
                    beep_verbs.append([current_title, *match_and_replace(current_group)])
                else:
                    count_of_groups += 1
                    verbs.append([current_title, *match_and_replace(current_group)])
                current_group = []
                current_title = ""

        if current_group:
            raise ValueError("Error: not in format of full blocks")
    print("SSID: ", ssid)
    print(f"Num of verb blocks: {count_of_groups}, in hex 0x{count_of_groups:02x}")
    return {
        "verbs": verbs,
        "beep": beep_verbs,
        "ssid": ssid,
        "count": count_of_groups,
    }


def _write_verb_groups(output_file, verb_groups):
    for group in verb_groups:
        for line in group:
            suffix = "\n"
            if line.startswith("0x") and all(c in string.hexdigits for c in line[2:].lower()):
                suffix = ",\n"
            output_file.write("\t" + line + suffix)


def format_to_file(filename, device_id, verb_table):
    """Write verb table to coreboot format.

    Args:
        filename: file to write the coreboot compatible C code to.
        device_id: codec vendor device ID, should be codec specific.
        verb_table: a dict that contains information from parse function.
    """
    header = """/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_HDA_VERB_H
#define MAINBOARD_HDA_VERB_H

#include <device/azalia_device.h>

const u32 cim_verb_data[] = {{
	/* coreboot specific header */
	0x{device_id:08x},	// Codec Vendor / Device ID:
	{ssid},	// Subsystem ID
	0x{nid:08x},	// Number of jacks (NID entries)
"""
    ending = """
AZALIA_ARRAY_SIZES;

#endif
"""

    with open(filename, "w") as output_file:
        output_file.write(
            header.format(device_id=device_id, ssid=verb_table['ssid'], nid=verb_table['count'])
        )
        _write_verb_groups(output_file, verb_table["verbs"])
        output_file.write("};\n")
        output_file.write("const u32 pc_beep_verbs[] = {\n")
        _write_verb_groups(output_file, verb_table["beep"])
        output_file.write("};\n")
        output_file.write(ending)


def main():
    parser = argparse.ArgumentParser(
        prog="Realtek verb table to coreboot",
        description=(
            "A small script to transform Realtek provided verb table text files"
            " to coreboot understandable C files"
        ),
    )
    parser.add_argument("-i", "--input", type=str, required=True)
    parser.add_argument("-o", "--output", type=str, required=True)
    parser.add_argument("-d", "--device_id", type=str, required=True)
    args = parser.parse_args()

    verb_table = parse(args.input)
    format_to_file(args.output, int(args.device_id, 16), verb_table)
    print("Conversion finished. Please check the output file to fix comments.")
    print(f"Formatting the file with `cros format {args.output}` is encouraged.")


if __name__ == "__main__":
    main()

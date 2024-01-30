#!/usr/bin/env vpython3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""

Check for blocked functions.

[VPYTHON:BEGIN]
python_version: "3.8"
[VPYTHON:END]
"""

import argparse
import importlib
import json
import os
from pathlib import Path
import re
import stat
import sys


# Path to the devtools directory.
DEVTOOLS_DIR = Path(__file__).resolve().parent
BLOCKED_FUNCTIONS_FILE = "blocked_functions.json"
PRE_SUBMIT = "pre-submit"
CHROMEOS_CHECKOUT_DIR = DEVTOOLS_DIR.parent.parent.parent.parent

if __name__ in ("__builtin__", "builtins"):
    # If repo imports us, the __name__ will be __builtin__, and the cwd will be
    # in the top level of the checkout (i.e. $CHROMEOS_CHECKOUT).  chromite will
    # be in that directory, so add it to our path.  This works whether we're
    # running the repo in $CHROMEOS_CHECKOUT/.repo/repo/ or a custom version in
    # a completely different tree.
    sys.path.insert(0, os.getcwd())


if __name__ == "__main__":
    # If we're run directly, we'll find chromite relative to the devtools dir
    # in $CHROMEOS_CHECKOUT.
    sys.path.insert(0, str(CHROMEOS_CHECKOUT_DIR))

sys.path.insert(0, str(CHROMEOS_CHECKOUT_DIR) + "/src/repohooks")

preupload = importlib.import_module("pre-upload")


def _get_file_lines(path):
    """Add line number to file"""
    with open(path) as file:
        content = file.read()
    return list(enumerate(content.splitlines(), 1))


def _get_affected_c_files(commit):
    """Returns list of c file paths that were modified/added.
    For c files, we want to check the source ".c" and ".h" files

    Args:
        commit: The commit

    Returns:
        A list of modified/added c files
    """

    path = os.getcwd()
    files = preupload._get_all_affected_files(commit, path)

    # Filter out symlinks and deletes.
    files = [x for x in files if not stat.S_ISLNK(int(x.dst_mode, 8))]
    files = [x for x in files if x.status != "D"]

    # git diff has two files, the src(original), and dst(modified)
    # Used the modified files if able.
    files = [x.dst_file if x.dst_file else x.src_file for x in files]

    # Filter for c libraries and sources (.c and .h) files
    files = [x for x in files if x.endswith(('.h', '.c'))]

    return files


def _read_blocked_functions_file(blocked_functions_file):
    """Read list of blocked functions and recommended replacement from file."""
    with open(blocked_functions_file) as f:
        blocked_functions_json = json.load(f)

    return blocked_functions_json["blocked_functions"]


def _check_functions_in_file(commit, file, blocked_functions):
    """Checks there are no blocked functions in a file being changed."""

    def _check_line(line):
        # Ignore lines that end with nocheck, typically in a comment.
        # This enables devs to bypass this check line by line.
        if line.endswith(" nocheck") or line.endswith(" nocheck */"):
            return None

        for function in blocked_functions:
            regex = r"(?<![a-zA-Z])" + function["function"] + r"\("
            match = re.search(regex, line)
            if match:
                return f'Found usage of "{function["function"]}", please replace it with "{function["replacement"]}"'
        return None

    error_list = []
    if commit is not None:
        file_lines = preupload._get_file_diff(file, commit)
    else:
        file_lines = _get_file_lines(file)
    for line_num, line in file_lines:
        result = _check_line(line)
        if result is not None:
            msg = f"{file}, line {line_num}: {result}"
            error_list.append(msg)
    return error_list


def _check_functions(commit, check_all_files):
    """Checks there are no blocked functions in commit content."""

    # Read blocked functions list.
    blocked_functions_file = DEVTOOLS_DIR / BLOCKED_FUNCTIONS_FILE
    blocked_functions = _read_blocked_functions_file(blocked_functions_file)

    if commit is not None:
        files = _get_affected_c_files(commit)

    if check_all_files:
        files = Path('.').glob('**/*.[ch]')

    error_list = []
    for file in files:
        errs = _check_functions_in_file(commit, file, blocked_functions)
        if errs:
            error_list.extend(errs)

    if error_list:
        print("Blocked functions found:")
        for error in error_list:
            print(error)

    return bool(error_list)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Check for unwanted C functions')
    parser.add_argument(
        '--commit',
        help='The git commit to check for unwanted C functions. If --commit is used, then the only the lines changed in the commit are checked.',
    )
    parser.add_argument(
        '--check_all_files',
        action='store_true',
        help='Check all .c and .h files under current directory',
    )
    args = parser.parse_args()
    sys.exit(_check_functions(args.commit, args.check_all_files))

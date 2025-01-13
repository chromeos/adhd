#!/usr/bin/env python3
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import glob
import json
import os


CFG_JSON_PATH = '/*/sw_build_config/**/project-config.json'
TESTDATA_NAME = 'testdata.rs'

CFG_JSON_HEADER = 'board,model,project,ucm-suffix,sku-id\n'
ANDROID_LIC = '''// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

'''
TESTDATA_HEADER = '''//! SKU full test data from CrOS
//!
//! (gen by $CROS/src/third_party/adhd/devtools/ucm_converter/ucm_suffix_map.py)

// [model, sku-id, ucm-suffix] per test sample
pub const TEST_DATA: &[[&str; 3]] = &[
'''


# NOTE: do not move this script elsewhere since it needs to locate the repository
#      "project" through the relative path
def get_project_dirpath():
    script_dir = os.path.dirname(os.path.realpath(__file__))
    project_dir = os.path.join(script_dir, '../../../../project/')
    return os.path.abspath(project_dir)


def generate_ucm_suffix_map(output_csv, output_test, boards):
    fout = open(output_csv, 'w')
    fout.write(CFG_JSON_HEADER)
    if output_test:
        ftest = open(output_test, 'w')
        ftest.write(ANDROID_LIC)
        ftest.write(TESTDATA_HEADER)

    count = 0
    project_dir = get_project_dirpath()

    for board in boards:
        path = os.path.join(project_dir, board)
        if not os.path.isdir(path):
            print(f'invalid board {board} will be skipped...')
            continue

        # Find out all project config JSON files
        json_path = path + CFG_JSON_PATH
        json_files = glob.glob(json_path, recursive=True)
        for json_file in json_files:
            print(f'parsing file: {json_file}...')
            with open(json_file, 'r') as fin:
                data = json.load(fin)

            cfgs = data['chromeos']['configs']
            for cfg in cfgs:
                try:
                    ucm_suffix = cfg['audio']['main']['ucm-suffix']
                    sku_id = cfg['identity']['sku-id']
                    model = cfg['name']
                    project = cfg['pvs']['project']

                    # skip if sku-id is >= 0x7FFFFEFF
                    if sku_id >= 2147483391:
                        print(f'Bad sku-id: {sku_id}')
                        continue

                    fout.write(f'{board},{model},{project},{ucm_suffix},{sku_id}\n')
                    if output_test:
                        ftest.write(f'    ["{model}", "{sku_id}", "{ucm_suffix}"],\n')
                    count += 1
                except KeyError as e:
                    print(repr(e))

    fout.close()

    if output_test:
        ftest.write(f']; /* DATA COUNT = {count} */\n')
        ftest.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate the map of UCM suffix sor all available HW SKUs"
    )
    parser.add_argument("boards", nargs='+', help="Board(s) to be generated")
    parser.add_argument('-o', '--output_csv', help="Path to the output CSV file")
    parser.add_argument(
        '-t', '--testdata', action='store_true', help="Also generate the file for testbench data"
    )
    parser.set_defaults(output_csv='output_map.csv')
    args = parser.parse_args()

    output_dir = os.path.dirname(os.path.abspath(args.output_csv))
    os.makedirs(output_dir, exist_ok=True)

    # Make testdata output filepath if --testdata is set
    output_test = os.path.join(output_dir, TESTDATA_NAME) if args.testdata else None

    generate_ucm_suffix_map(args.output_csv, output_test, args.boards)

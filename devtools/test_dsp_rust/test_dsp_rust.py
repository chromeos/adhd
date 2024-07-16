#!/usr/bin/env vpython3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import glob
import os
import shutil
import subprocess
import sys


parser = argparse.ArgumentParser(
    description='Build the same target with C and Rust from two adhd directory.'
)
parser.add_argument(
    'test_target', metavar='"test_target.c"', type=str, help='Build target to be test'
)
parser.add_argument(
    'adhd_dir',
    metavar='"comparing adhd directory"',
    type=str,
    help='Origin adhd directory with C libraries',
)
parser.add_argument(
    'testfiles_dir', metavar='"testfiles directory"', type=str, help='Directory with .wav testfiles'
)
args = parser.parse_args()

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
CUR_ADHD = os.path.join(SCRIPT_DIR, "../..")

if (not os.path.isfile(os.path.join(CUR_ADHD, "cras/src/dsp/tests/", args.test_target))) or (
    not os.path.isfile(os.path.join(args.adhd_dir, "cras/src/dsp/tests/", args.test_target))
):
    raise SystemExit("Test targets not found")

if not os.path.isfile(os.path.join(SCRIPT_DIR, "cmpraw")):
    subprocess.run(["bazel", "build", "//cras/src/dsp/tests:cmpraw"], cwd=CUR_ADHD, check=True)
    shutil.copy("bazel-bin/cras/src/dsp/tests/cmpraw", SCRIPT_DIR)
BUILD_TARGET = args.test_target.removesuffix(".c")
try:
    os.mkdir(os.path.join(SCRIPT_DIR, "sandbox"))
except FileExistsError:
    pass

subprocess.run(["bazel", "build", "//cras/src/dsp/tests:" + BUILD_TARGET], cwd=CUR_ADHD)
shutil.copy(
    os.path.join(CUR_ADHD, "bazel-bin/cras/src/dsp/tests/", BUILD_TARGET),
    os.path.join(SCRIPT_DIR, "sandbox/rust"),
)
subprocess.run(["bazel", "build", "//cras/src/dsp/tests:" + BUILD_TARGET], cwd=args.adhd_dir)
shutil.copy(
    os.path.join(args.adhd_dir, "bazel-bin/cras/src/dsp/tests/", BUILD_TARGET),
    os.path.join(SCRIPT_DIR, "sandbox/c"),
)
os.chdir(SCRIPT_DIR)
for file in os.listdir(args.testfiles_dir):
    f = os.path.join(args.testfiles_dir, file)
    subprocess.run(
        [
            "sox",
            f,
            "--bits",
            "16",
            "--encoding",
            "signed-integer",
            "--endian",
            "little",
            "./sandbox/test_file.raw",
        ],
        cwd=SCRIPT_DIR,
        check=True,
    )
    subprocess.run(
        ["./sandbox/rust", "sandbox/test_file.raw", "sandbox/rust_output.raw"],
        cwd=SCRIPT_DIR,
        check=True,
    )
    subprocess.run(
        ["./sandbox/c", "sandbox/test_file.raw", "sandbox/c_output.raw"], cwd=SCRIPT_DIR, check=True
    )
    print(file + " result:", end='', flush=True)
    subprocess.run(
        ["./cmpraw", "sandbox/c_output.raw", "sandbox/rust_output.raw"], cwd=SCRIPT_DIR, check=True
    )
shutil.rmtree("sandbox")
print("Remove generated binary with the command if there is no further testing:")
print("\trm -f devtools/test_dsp_rust/cmpraw")

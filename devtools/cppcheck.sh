#!/bin/bash

# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script runs cppcheck with the arguments used for COP
# Usage: cppcheck.bash <absolute compile_commands.json filepath>
ENABLE_ARGS="--enable=warning,performance,portability,information"
IGNORE_PATH_ARGS=("-iexternal"
"-icras/src/tests"
"-ibazel-out"
"-icras/benchmark")
SUPPRESS_ARGS=("--suppress=uninitvar"
"--suppress=unknownMacro"
"--suppress=missingInclude"
"--suppress=missingIncludeSystem"
"--suppress=syntaxError"
"--suppress=uninitMemberVarPrivate"
"--suppress=internalAstError")
PROJECT_ARGS="--project=$1"
CPPCHECK_ARGS=("${ENABLE_ARGS}"
"${IGNORE_PATH_ARGS[@]}"
"${SUPPRESS_ARGS[@]}"
"${PROJECT_ARGS}"
"--error-exitcode=5"
"--quiet")

bazel run "@cppcheck" "--" "${CPPCHECK_ARGS[@]}"

#!/bin/bash
set -eux

OUT="/out"
SRC_DIR="${1:-/workspace/adhd}"
REPO_ROOT=$(mktemp -d -t chromiumos.XXXXXXXXXX)
THIRD_PARTY="$REPO_ROOT/src/third_party"
ADHD_ROOT="$THIRD_PARTY/adhd"

mkdir -p "$OUT"

mkdir -p "$THIRD_PARTY"
cp -r "$SRC_DIR" "$ADHD_ROOT"

# Build
cd "$ADHD_ROOT"
bazel run //:compdb
bazel build //... --config=local-clang
python /compdb_fixup.py

# Metadata
cp compile_commands.json "$OUT"/
git rev-parse HEAD > "$OUT"/revision

# Extract for superproject
export KYTHE_CORPUS=chromium.googlesource.com/chromiumos/codesearch//main
export KYTHE_OUTPUT_DIRECTORY=$(mktemp -d -t kythe.XXXXXXXXXX)
export KYTHE_ROOT_DIRECTORY="$REPO_ROOT"
/opt/kythe/tools/runextractor compdb -extractor /opt/kythe/extractors/cxx_extractor
/opt/kythe/tools/kzip merge --output="$OUT/merged-codesearch.kzip" --recursive "$KYTHE_OUTPUT_DIRECTORY"

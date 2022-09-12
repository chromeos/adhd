#!/bin/bash

# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

function usage() {
  echo "Script to create an ucm-config template."
  echo
  echo "Usage: $0 [-h] [-f DIR] [-p PLAYBACK_PCM] [-c CAPTURE_PCM] CARD_NAME"
  echo "options:"
  echo "h   Print this Help."
  echo "f   Specify the directory to put the created ucm-config. Default:\$PWD"
  echo "p   Specify the PlaybackPCM in the config."
  echo "c   Specify the CapturePCM in the config."
}

DIR=$PWD
PLAYBACK_PCM=""
CAPTURE_PCM=""

while getopts "hf:c:p:" opt; do
  case "$opt" in
  h) usage && exit 0;;
  p) PLAYBACK_PCM="$OPTARG";;
  c) CAPTURE_PCM="$OPTARG";;
  f) DIR="$OPTARG";;
  \?) echo "Error: Invalid option" && usage && exit 1;;
  esac
done
shift $((OPTIND-1))

if [ ! -z "$1" ]; then
    CARD_NAME=$1
    echo "Creating ucm-config for $1."
else
    echo "You need to specify card name" && usage && exit 1
fi

if [ ! -d "$DIR" ]; then
  echo "$DIR doesn't exist or is not a directory."
  $( usage )
  exit 1
fi

mkdir "$DIR/$CARD_NAME"
cd "$DIR/$CARD_NAME"

CONF="Comment \"$CARD_NAME\"

SectionUseCase.\"HiFi\" {
        File \"HiFi.conf\"
        Comment \"Default\"
}"

echo ""
echo "$CARD_NAME.conf"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "$CONF"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

cat > "$CARD_NAME.conf" << EOF
$CONF
EOF

HIFI="SectionVerb {
        Value {
                FullySpecifiedUCM \"1\"
        }

        EnableSequence [
        ]

        DisableSequence [
        ]
}

SectionDevice.\"$CARD_NAME Output\".0 {
        Value {
                PlaybackPCM \"$PLAYBACK_PCM\"
        }
}

SectionDevice.\"$CARD_NAME Input\".0 {
        Value {
                CapturePCM \"$CAPTURE_PCM\"
        }
}"

echo ""
echo "HiFi.conf"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "$HIFI"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

cat > HiFi.conf << EOF
$HIFI
EOF

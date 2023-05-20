// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generate go/cros-cop compatible build configurations.
package main

import (
	"bytes"
	"encoding/json"
	"log"
	"os"

	"chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/quick-verifier/build"
	"github.com/alecthomas/kingpin/v2"
	"google.golang.org/protobuf/encoding/protojson"
)

func main() {
	path := kingpin.Arg("path", "Path to build.yaml").Required().String()
	kingpin.Parse()

	b := build.MakeCopBuild()

	bjson, err := protojson.Marshal(b)
	if err != nil {
		log.Fatal("Failed to marshal JSON: ", err)
	}
	var buf bytes.Buffer
	if err := json.Indent(&buf, bjson, "", "  "); err != nil {
		log.Fatal("Failed to indent JSON: ", err)
	}

	if err := os.WriteFile(*path, buf.Bytes(), 0644); err != nil {
		log.Fatal("Failed to write build: ", err)
	}
}

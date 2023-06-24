// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestQueryBuilds(t *testing.T) {
	br, err := queryBuilds("t-h2GG-vvYS72O7cXkPKxWHA")
	if err != nil {
		t.Fatal(err)
	}
	const want = `0/1 builds passed
1 failed builds: default
Logs: https://console.cloud.google.com/cloud-build/builds?project=chromeos-audio-qv&query=tags%3Dt-h2GG-vvYS72O7cXkPKxWHA
* Step [archlinux-gcc:1](https://console.cloud.google.com/cloud-build/builds/9c1f4152-166b-4ffc-a95e-bb60e0c37d9c;step=10?project=94508773714) of build default ended with status: FAILURE
`
	if diff := cmp.Diff(want, br.String()); diff != "" {
		t.Fatalf("build output differ: -want +got:\n%s", diff)
	}
}

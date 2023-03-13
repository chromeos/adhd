// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bulkreview

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestParseChange(t *testing.T) {
	table := []struct {
		input  string
		output Change
	}{
		{"12345", Change{"chromium", "12345", 0}},
		{"*23456", Change{"chrome-internal", "23456", 0}},
		{"chromium:9999", Change{"chromium", "9999", 0}},
		{"chrome-internal:9999", Change{"chrome-internal", "9999", 0}},
		{"https://crrev.com/c/1337", Change{"chromium", "1337", 0}},
		{"https://crrev.com/i/1337", Change{"chrome-internal", "1337", 0}},
	}

	for _, item := range table {
		t.Run(item.input, func(t *testing.T) {
			change, err := ParseChange(item.input)
			if err != nil {
				t.Fatalf("unexpected error parsing %q: %s", item.input, err)
			}

			if diff := cmp.Diff(item.output, change); diff != "" {
				t.Fatalf("parse %q; -want +got\n%s", item.input, diff)
			}
		})
	}
}

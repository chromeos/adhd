// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bulkreview

import (
	"testing"
)

func TestDomainMatch(t *testing.T) {
	table := []struct {
		domain string
		host   string
	}{
		{"chromium-review.googlesource.com", "chromium-review.googlesource.com"},
		{".googlesource.com", "chromium-review.googlesource.com"},
		{".googlesource.com", "googlesource.com"},
	}

	for _, item := range table {
		if !domainMatch(item.domain, item.host) {
			t.Errorf("domainMatch(%q, %q) != true", item.domain, item.host)
		}
	}
}

func TestDomainNotMatch(t *testing.T) {
	table := []struct {
		domain string
		host   string
	}{
		{"chromium-review.googlesource.com", "googlesource.com"},
		{"googlesource.com", "chromium-review.googlesource.com"},
		{"example.com", ".googlesource.com"},
		{"example.com", "googlesource.com"},
	}

	for _, item := range table {
		if domainMatch(item.domain, item.host) {
			t.Errorf("domainMatch(%q, %q) != false", item.domain, item.host)
		}
	}
}

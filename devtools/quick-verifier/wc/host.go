// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wc

import "fmt"

type GerritHost string

func (h GerritHost) URL() string {
	return fmt.Sprintf("https://%s-review.googlesource.com/", h)
}

const (
	gerritHostChromium       GerritHost = "chromium"
	gerritHostChromeInternal GerritHost = "chrome-internal"
)

var gerritHosts = []GerritHost{
	gerritHostChromium,
	gerritHostChromeInternal,
}

// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bulkreview

import (
	"errors"
	"fmt"
	"strings"
)

// Change identifys a change on Gerrit
type Change struct {
	GerritHost GerritHost // e.g. chromium, chrome-internal
	ID         string     // change ID or number

	Revision int // The revision we should vote on
}

var _ fmt.Stringer = Change{}

func (cl Change) String() string {
	return string(cl.GerritHost) + ":" + cl.ID
}

// returns the ID as understood by `cros tryjob`
func (cl Change) CrosTryjobID() string {
	switch cl.GerritHost {
	case gerritHostChromium:
		return cl.ID
	case gerritHostChromeInternal:
		return "*" + cl.ID
	default:
		panic("not a cros gerrit host")
	}
}

func (cl Change) URL() string {
	switch cl.GerritHost {
	case gerritHostChromium:
		return "https://crrev.com/c/" + cl.ID
	case gerritHostChromeInternal:
		return "https://crrev.com/i/" + cl.ID
	default:
		return fmt.Sprintf("https://%s-review.googlesource.com/%s", cl.GerritHost, cl.ID)
	}
}

// ParseChange parses a change in the following form:
// - 12345
// - *12345
// - chromium:12345
// - chrome-internal:12345
// - https://crrev.com/c/12345
// - https://crrev.com/i/12345
func ParseChange(s string) (Change, error) {
	if isDigit(s) {
		return Change{gerritHostChromium, s, 0}, nil
	}

	matchers := []struct {
		prefix     string
		gerritHost GerritHost
	}{
		{"*", gerritHostChromeInternal},
		{"chromium:", gerritHostChromium},
		{"chrome-internal:", gerritHostChromeInternal},
		{"https://crrev.com/c/", gerritHostChromium},
		{"https://crrev.com/i/", gerritHostChromeInternal},
	}

	for _, m := range matchers {
		before, after, found := strings.Cut(s, m.prefix)
		if found && before == "" {
			if !isDigit(after) {
				return Change{}, fmt.Errorf("%q not followed by a number", m.prefix)
			}
			return Change{
				GerritHost: m.gerritHost,
				ID:         after,
			}, nil
		}
	}

	return Change{}, errors.New("unsupported change format")
}

func isDigit(s string) bool {
	return strings.Trim(s, "0123456789") == ""
}

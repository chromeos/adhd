// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	qt "github.com/frankban/quicktest"
)

func TestParse(t *testing.T) {
	p := parser{
		args: []string{"clang++", "-o", "a.out", "a.cc"},
		i:    1,
	}
	output, found := p.consumeShortFlag("-o")
	qt.Check(t, found, qt.Equals, true)
	qt.Check(t, output, qt.Equals, "a.out")
}

func TestCompilation(t *testing.T) {
	c := parseCompilation(Execution{
		Arguments: []string{"clang++", "-o", "a.out", "a.cc"},
	})
	qt.Check(t, c.output, qt.Equals, "a.out")
	qt.Check(t, c.inputs, qt.DeepEquals, []string{"a.cc"})
}

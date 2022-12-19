// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"os/exec"
	"strings"
)

func (c *compilation) findHeaders() []string {
	cmd := exec.Command(
		c.executable,
		"-fsyntax-only",
		c.inputs[0],
		"-MF", "/dev/stdout",
		"-MMD",
		"-MT", "out",
	)
	cmd.Args = append(cmd.Args, c.flags()...)
	cmd.Dir = "../cras/src"
	outputBytes, err := cmd.Output()
	if err != nil {
		log.Fatal(cmd, err)
	}
	output := string(outputBytes)

	deps, ok := strings.CutPrefix(output, "out: ")
	if !ok {
		log.Fatal(output)
	}
	return strings.FieldsFunc(
		deps,
		func(r rune) bool {
			return strings.ContainsRune(" \n\\", r)
		},
	)
}

// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"io"
	"log"
	"path"
	"strings"

	"go.starlark.net/syntax"
)

type node interface {
	render(w io.Writer, depth int)
}

func indent(w io.Writer, depth int) {
	io.WriteString(w, strings.Repeat("    ", depth))
}

type call struct {
	label string
	args  map[string]node
}

func (c *call) render(w io.Writer, depth int) {
	fmt.Fprintf(w, "%s(\n", c.label)
	for key, value := range c.args {
		indent(w, depth+1)
		fmt.Fprintf(w, "%s = ", key)
		value.render(w, depth+1)
		io.WriteString(w, ",\n")
	}
	indent(w, depth)
	io.WriteString(w, ")")
}

type stringLiteral string

func (s stringLiteral) render(w io.Writer, depth int) {
	io.WriteString(w, syntax.Quote(string(s), false))
}

type list []node

func (l list) render(w io.Writer, depth int) {
	io.WriteString(w, "[\n")
	for _, item := range l {
		indent(w, depth+1)
		item.render(w, depth+1)
		io.WriteString(w, ",\n")
	}
	indent(w, depth)
	io.WriteString(w, "]")
}

func stringLiterals(l []string) list {
	var result list
	for _, item := range l {
		result = append(result, stringLiteral(item))
	}
	return result
}

func needsSectionsHack(name string) bool {
	switch name {
	case "ewma_power_unittest", "cras_client_unittest":
		return true
	default:
		return false
	}
}

var brokenTests = map[string]bool{}

func ccTestRule(name string, srcs []string, includeDirs []string, libs []string, linkFlags []string, defines []string) string {
	deps := list{
		stringLiteral(":test_support"),
	}
	var bazelSrcs list
	for _, inc := range includeDirs {
		switch inc {
		case "src":
			// There is no source here, ignore
		case "src/common", "src/server", "src/dsp", "src/server/config", "src/libcras", "src/plc":
			deps = append(deps, stringLiteral(bazelDir(inc)+":all_headers"))
		default:
			log.Panic(inc)
		}
	}
	for _, lib := range libs {
		switch lib {
		case "gtest", "gtest_main", "speexdsp":
			deps = append(deps, stringLiteral("@pkg_config//:"+lib))
		case "asound":
			deps = append(deps, stringLiteral("@pkg_config//:alsa"))
		case "pthread", "m", "rt":
			// Do nothing
		case "iniparser":
			// This should be superfluous
		default:
			log.Panic(lib)
		}
	}
	for _, src := range srcs {
		if path.Dir(src) == "src/tests" {
			bazelSrcs = append(bazelSrcs, stringLiteral(":"+path.Base(src)))
		} else {
			bazelSrcs = append(bazelSrcs, stringLiteral(bazelFile(src)))
		}
	}

	rule := call{
		label: "cc_test",
		args: map[string]node{
			"name": stringLiteral(name),
			"srcs": bazelSrcs,
			"deps": deps,
			// "linkopts": stringLiterals(linkFlags),
		},
	}
	if needsSectionsHack(name) {
		rule.args["copts"] = stringLiterals([]string{"-fdata-sections", "-ffunction-sections"})
	}
	if brokenTests[name] {
		rule.args["tags"] = list{stringLiteral("broken")}
	}

	var b strings.Builder
	rule.render(&b, 0)
	return b.String()
}

func bazelDir(p string) string {
	return "//" + p
}

func bazelFile(p string) string {
	return bazelDir(path.Dir(p)) + ":" + path.Base(p)
}

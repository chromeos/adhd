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

var needsSectionsHack = map[string]bool{
	"ewma_power_unittest":     true,
	"cras_client_unittest":    true,
	"floop_iodev_unittest":    true,
	"sr_bt_adapters_unittest": true,
	"bt_policy_unittest":      true,
	"bt_device_unittest":      true,
	"bt_io_unittest":          true,
	"hfp_manager_unittest":    true,
}

var brokenTests = map[string]bool{}

func ccTestRule(name string, srcs []string, includeDirs []string, libs []string, linkFlags []string, defines []string, headers []string, dependencies []string) string {
	deps := list{
		stringLiteral(":test_support"),
	}
	deps = append(deps, stringLiterals(dependencies)...)
	var bazelSrcs list
	var wantIniparser bool
	var wantDbus bool
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
			wantIniparser = true
		case "dbus-1":
			wantDbus = true
		default:
			log.Panic(lib)
		}
	}
	for _, hdr := range headers {
		switch path.Base(hdr) {
		case "iniparser.h":
			wantIniparser = true
		case "dbus.h":
			wantDbus = true
		}
	}
	if wantIniparser {
		deps = append(deps, stringLiteral("@iniparser"))
	}
	if wantDbus {
		deps = append(deps, stringLiteral("@pkg_config//:dbus-1"))
	}
	for _, src := range srcs {
		if path.Dir(src) == "src/tests" {
			bazelSrcs = append(bazelSrcs, stringLiteral(":"+path.Base(src)))
		} else {
			if src == "src/server/cras_mix.c" || src == "src/server/cras_mix_ops.c" {
				// Should not be referenced directly
				continue
			}
			bazelSrcs = append(bazelSrcs, stringLiteral(bazelFile(src)))
			if src == "src/server/cras_dbus_control.c" {
				bazelSrcs = append(bazelSrcs, stringLiteral("//src/common:cras_dbus_bindings.h"))
			}
		}
	}

	// More hacks
	switch name {
	case "dsp_unittest":
		deps = append(deps, stringLiteral("//src/dsp/tests:all_headers"))
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
	if needsSectionsHack[name] {
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

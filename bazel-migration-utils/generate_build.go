// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"path"
	"strings"

	"golang.org/x/exp/slices"
)

type Execution struct {
	Executable string
	Arguments  []string
	WorkingDir string `json:"working_dir"`
}

type Event struct {
	Started *struct {
		Execution Execution
	}
}

type profile struct {
	compilations map[string]*compilation
	links        map[string]*compilation
}

func newProfile() *profile {
	return &profile{
		compilations: make(map[string]*compilation),
		links:        make(map[string]*compilation),
	}
}

func (p *profile) handleClang(e Execution) {
	if len(e.Arguments) == 2 && e.Arguments[1] == "-V" {
		// clang++ -V
		return
	}
	if !strings.HasSuffix(e.WorkingDir, "cras/src") {
		// Likely from cargo
		return
	}
	compilation := parseCompilation(e)
	if slices.Contains(e.Arguments, "-c") {
		p.compilations[compilation.output] = compilation
	} else {
		p.links[compilation.output] = compilation
	}
}

func (p *profile) handle(e Execution) {
	switch path.Base(e.Arguments[0]) {
	case "make",
		"sh",
		"sed",
		"cp",
		"basename",
		"xxd",
		"rm",
		"mv",
		"cargo",
		"rustc",
		"mkdir",
		"cat",
		"build-script-build",
		"grep",
		"git",
		"diff",
		"sort",
		"ar",
		"ranlib",
		"ln",
		"tr",
		"chmod",
		"collect2",
		"ld",
		"find",
		"wc",
		"expr",
		"gawk":
	case "clang", "cc", "clang++":
		p.handleClang(e)
	default:
		log.Fatal(e)
	}
}

var archiveMap = map[string]string{
	"./.libs/libcrasmix_fma.a":   "//src/server:cras_mix",
	"./.libs/libcrasmix_avx2.a":  "//src/server:cras_mix",
	"./.libs/libcrasmix_avx.a":   "//src/server:cras_mix",
	"./.libs/libcrasmix_sse42.a": "//src/server:cras_mix",
	"./.libs/libcrasmix.a":       "//src/server:cras_mix",
}

func (p *profile) analyzeBuild(link *compilation) [2]string {
	var nothing = [2]string{"", ""}

	if !strings.HasSuffix(link.output, "_unittest") {
		// Only handle unit tests for now
		return nothing
	}

	var sources []string
	var includeDirs []string
	var headers []string
	var defines []string
	var dependencies []string
	var unsupported error

	// Find the .c/.cc source for the .o objects
	for _, input := range link.inputs {
		if strings.HasSuffix(input, ".a") {
			if dep, ok := archiveMap[input]; !ok {
				unsupported = errors.New(input)
			} else {
				dependencies = append(dependencies, dep)
			}

			continue
		}
		comp, ok := p.compilations[input]
		if !ok {
			log.Panic(input, link)
		}
		if len(comp.inputs) != 1 {
			log.Fatal(comp.execution.Arguments)
		}
		sources = append(sources, canonPath(comp.inputs[0]))
		for _, hdr := range comp.findHeaders() {
			headers = append(headers, canonPath(hdr))
		}
		for _, inc := range comp.includeDirs {
			if strings.HasPrefix(inc, "/") {
				continue
			}
			includeDirs = append(includeDirs, canonPath(inc))
		}
		defines = append(defines, comp.defines...)
	}

	slices.Sort(includeDirs)
	includeDirs = slices.Compact(includeDirs)
	slices.Sort(headers)
	headers = slices.Compact(headers)
	slices.Sort(defines)
	defines = slices.Compact(defines)
	slices.Sort(dependencies)
	dependencies = slices.Compact(dependencies)

	for _, hdr := range headers {
		if path.Dir(hdr) == "src/tests" {
			sources = append(sources, hdr)
		}
	}

	if unsupported != nil {
		log.Printf("%s is unsupported: %s", link.output, unsupported)
		return nothing
	}

	// log.Println(link.output, sources, dependencies)

	if slices.Contains(includeDirs, "src/server/rust/include") {
		return nothing
	}

	return [2]string{link.output, ccTestRule(link.output, sources, includeDirs, link.libraries, link.linkArgs, defines, headers, dependencies)}
}

func main() {
	log.SetFlags(log.Lshortfile)

	b, err := os.ReadFile("../cras/events.json")
	if err != nil {
		log.Fatal(err)
	}
	r := bufio.NewReader(bytes.NewReader(b))
	p := newProfile()
	for {
		line, err := r.ReadBytes('\n')
		if err != nil {
			if err == io.EOF {
				break
			}
			log.Fatal(err)
		}
		var event Event
		if err := json.Unmarshal(line, &event); err != nil {
			log.Fatal(err)
		}
		if event.Started == nil {
			continue
		}

		execution := event.Started.Execution
		p.handle(execution)
	}

	fmt.Print(`# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

`)

	(&call{
		label: "cc_library",
		args: map[string]node{
			"name": stringLiteral("test_support"),
			"defines": stringLiterals([]string{
				`CRAS_UT_TMPDIR=\"/tmp\"`,
				`CRAS_SOCKET_FILE_DIR=\"/run/cras\"`,
			}),
			"linkopts": stringLiterals([]string{
				"-Wl,--gc-sections",
				"-lm",
			}),
			"deps": stringLiterals([]string{
				"//:build_config",
			}),
		},
	}).render(os.Stdout, 0)
	fmt.Println()

	var links []*compilation
	for _, link := range p.links {
		links = append(links, link)
	}
	var results [][2]string
	for result := range parallelMap(p.analyzeBuild, links) {
		if result[0] != "" {
			results = append(results, result)
		}
	}
	slices.SortFunc(results, func(a, b [2]string) bool { return a[0] < b[0] })
	for _, result := range results {
		fmt.Println(result[1])
	}
}

func canonPath(p string) string {
	if strings.HasPrefix(p, "/") {
		return p
	}
	return path.Clean(path.Join("src", p))
}

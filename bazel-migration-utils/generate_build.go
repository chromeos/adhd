// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"bytes"
	"encoding/json"
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

	for _, link := range p.links {
		if !strings.HasSuffix(link.output, "_unittest") {
			// Only handle unit tests for now
			continue
		}
		// Find the .c/.cc source for the .o objects
		for _, input := range link.inputs {
			if strings.HasSuffix(input, ".a") {
				// TODO: handle library archives
				continue
			}
			comp, ok := p.compilations[input]
			if !ok {
				log.Panic(input, link)
			}
			log.Println(comp.output, comp.inputs)
		}
		fmt.Println(link.inputs)
	}
}

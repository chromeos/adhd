// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"debug/elf"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"path"
	"path/filepath"
	"slices"
	"strings"

	"go.skia.org/skia/bazel/exporter/build_proto/analysis_v2"
	"google.golang.org/protobuf/proto"
)

type withID interface {
	GetId() uint32
}

func collect[T withID](items []T) map[uint32]T {
	result := map[uint32]T{}
	for _, item := range items {
		result[item.GetId()] = item
	}
	return result
}

// buildGraph is a wrapper for analysis_v2.ActionGraphContainer.
// https://github.com/bazelbuild/bazel/blob/9bf19e1472b406483fe9c1604f21e0a8d7f18f7c/src/main/protobuf/analysis_v2.proto#L25
type buildGraph struct {
	targets       map[uint32]*analysis_v2.Target
	depSetOfFiles map[uint32]*analysis_v2.DepSetOfFiles
	artifacts     map[uint32]*analysis_v2.Artifact
	pathFragments map[uint32]*analysis_v2.PathFragment
}

func newBuildGraph(c *analysis_v2.ActionGraphContainer) *buildGraph {
	return &buildGraph{
		targets:       collect(c.Targets),
		depSetOfFiles: collect(c.DepSetOfFiles),
		artifacts:     collect(c.Artifacts),
		pathFragments: collect(c.PathFragments),
	}
}

func (bg *buildGraph) artifactPath(artifactID uint32) string {
	pathFragmentID := bg.artifacts[artifactID].PathFragmentId

	var labels []string
	for pathFragmentID != 0 {
		f := bg.pathFragments[pathFragmentID]
		labels = append(labels, f.Label)
		pathFragmentID = f.ParentId
	}
	slices.Reverse(labels)
	return strings.Join(labels, "/")
}

// translationUnit represents the compilation of a single C/C++ source file.
type translationUnit struct {
	// The bazel build target label.
	TargetLabel string `json:"target_label"`
	// Path to the source file. Relative to adhd/.
	Source string `json:"source"`
	// Input files used to compile the source file, as reported by Bazel.
	// Relative to adhd/.
	InputsBazel []string `json:"inputs_bazel"`
	// Input files used to compile the source file, as detected from make
	// depfiles. Relative to adhd/.
	InputsMakefile []string `json:"inputs_makefile"`
	// Symbols provided by this translation unit.
	ProvidedSymbols []string `json:"provided_symbols"`
	// Symbols required by this translation unit.
	DependantSymbols []string `json:"dependant_symbols"`
}

// parseMakeDepFile parses a .d file.
// Returns the list of dependencies, which are usually headers.
func parseMakeDepFile(path string) []string {
	b, err := os.ReadFile(path)
	if err != nil {
		log.Panicf("error reading %s: %s", path, err)
	}
	_, after, found := strings.Cut(string(b), ":")
	if !found {
		log.Panicf("%s: %q not found in %q", path, ":", string(b))
	}

	var result []string
	for _, dep := range strings.FieldsFunc(after, func(r rune) bool { return r == ' ' || r == '\n' || r == '\\' }) {
		if filepath.IsAbs(dep) {
			// Ignore system headers.
			continue
		}
		result = append(result, dep)
	}
	return result
}

func parseObjectFile(path string) (providedSymbols, dependantSymbols []string) {
	f, err := elf.Open(path)
	if err != nil {
		panic(err)
	}
	defer f.Close()

	symbols, err := f.Symbols()
	if err != nil {
		panic(err)
	}
	for _, sym := range symbols {
		symBind := elf.ST_BIND(sym.Info)
		if symBind != elf.STB_GLOBAL {
			continue
		}
		symType := elf.ST_TYPE(sym.Info)
		switch symType {
		case elf.STT_FUNC:
			providedSymbols = append(providedSymbols, sym.Name)
		case elf.STT_NOTYPE:
			dependantSymbols = append(dependantSymbols, sym.Name)
		}
	}
	return providedSymbols, dependantSymbols
}

func run(actionsProtoPath string, adhdDir string) {
	b, err := os.ReadFile(actionsProtoPath)
	if err != nil {
		panic(err)
	}

	var agc analysis_v2.ActionGraphContainer

	if err := proto.Unmarshal(b, &agc); err != nil {
		panic(err)
	}

	bg := newBuildGraph(&agc)

	var tus []*translationUnit

	for _, a := range agc.Actions {

		target := bg.targets[a.TargetId]

		tu := &translationUnit{
			TargetLabel: target.Label,
		}
		tus = append(tus, tu)

		processInput := func(path string) {
			if strings.HasPrefix(path, "external/local_config_cc") {
				// Ignore Bazel stuff.
				return
			}
			tu.InputsBazel = append(tu.InputsBazel, path)
		}
		var visit func(depSetID uint32)
		visit = func(depSetID uint32) {
			files := bg.depSetOfFiles[depSetID]
			for _, dID := range files.DirectArtifactIds {
				processInput(bg.artifactPath(dID))
			}
			for _, tID := range files.TransitiveDepSetIds {
				visit(tID)
			}
		}
		for _, dsID := range a.InputDepSetIds {
			visit(dsID)
		}

		for _, oID := range a.OutputIds {
			output := bg.artifactPath(oID)
			switch path.Ext(output) {
			case ".d":
				tu.InputsMakefile = parseMakeDepFile(filepath.Join(adhdDir, output))
			case ".o":
				tu.ProvidedSymbols, tu.DependantSymbols = parseObjectFile(filepath.Join(adhdDir, output))
			}
		}

		// Assume the first input file in the depfile
		// is the source file of the translation unit.
		tu.Source = tu.InputsMakefile[0]
		if !strings.HasSuffix(tu.Source, ".c") && !strings.HasSuffix(tu.Source, ".cc") {
			log.Panicf("Bad tu.Source guess %q", tu.Source)
		}
	}

	j, err := json.MarshalIndent(tus, "", "  ")
	if err != nil {
		log.Fatal("Cannot marshal JSON: ", err)
	}
	fmt.Println(string(j))
}

func main() {
	actionsProtoPath := flag.String("actions-proto", "", `Output of bazel aquery 'mnemonic(CppCompile, //...)' --output=proto --include_commandline=false`)
	adhdDir := flag.String("adhd-dir", "", "Path to adhd/")
	flag.Parse()
	run(*actionsProtoPath, *adhdDir)
}

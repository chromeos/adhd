// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"strings"
)

type parser struct {
	args []string
	i    int
}

func newParser(e Execution) *parser {
	return &parser{
		args: e.Arguments,
		i:    1,
	}
}

// get the current arg
func (p *parser) get() string {
	return p.args[p.i]
}

// get the current arg and proceed
func (p *parser) consume() string {
	p.i++
	return p.args[p.i-1]
}

// Parses something like:
// - -Isrc/common
// - -I src/common
func (p *parser) consumeShortFlag(prefix string) (value string, found bool) {
	value, found = strings.CutPrefix(p.args[p.i], prefix)
	if !found {
		return "", false
	}
	p.i++
	if value == "" {
		value = p.args[p.i]
		p.i++
	}
	return value, found
}

func (p *parser) done() bool {
	return p.i == len(p.args)
}

// try to consume a flag that is uninteresting for dependency analysis
func (p *parser) consumeExtra() (flags []string, found bool) {
	consumeOne := func() ([]string, bool) {
		return []string{p.consume()}, true
	}

	// Consume exact matches
	if p.get() == "-L" {
		// Consume the flag and the argument following it
		return []string{p.consume(), p.consume()}, true
	}

	// Consume prefixes
	for _, prefix := range []string{
		"-O", // -O2
		"-g",
		"-f",    // -ffunction-sections
		"-m",    // -mavx
		"-std=", // std=gnu11
		"-pie",
		"-nodefaultlibs",
		"-shared",
		"-L",
	} {
		if strings.HasPrefix(p.get(), prefix) {
			return consumeOne()
		}
	}
	if strings.HasPrefix(p.get(), "-W") && !strings.HasPrefix(p.get(), "-Wl") {
		return consumeOne()
	}
	return nil, false
}

// Remove dependency flags
func (p *parser) consumeDependency() bool {
	switch p.get() {
	case "-MD", "-MP", "-c":
		p.consume()
		return true
	case "-MT", "-MF":
		p.consume()
		p.consume()
		return true
	default:
		return false
	}
}

// Something like:
//
//	clang -DPACKAGE_NAME="cras" -DPACKAGE_TARNAME="cras"
//	-DPACKAGE_VERSION="0.1" -DPACKAGE_STRING="cras 0.1"
//	-DPACKAGE_BUGREPORT="chromeos-audio-bugs@google.com"
//	-DPACKAGE_URL="http://www.chromium.org/" -DHAVE_STDIO_H=1
//	-DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1 -DHAVE_INTTYPES_H=1
//	-DHAVE_STDINT_H=1 -DHAVE_STRINGS_H=1 -DHAVE_SYS_STAT_H=1
//	-DHAVE_SYS_TYPES_H=1 -DHAVE_UNISTD_H=1 -DSTDC_HEADERS=1
//	-DHAVE_DLFCN_H=1 -DLT_OBJDIR=".libs/" -DHAVE_INIPARSER_INIPARSER_H=1
//	-DHAVE_INIPARSER_INIPARSER_H=1 -DHAVE_LADSPA_H=1 -DHAVE_LIBASOUND=1
//	-DALSA_PLUGIN_DIR="/usr/local/lib/alsa-lib"
//	-DCRAS_CONFIG_FILE_DIR="/usr/local/etc/cras"
//	-DCRAS_SOCKET_FILE_DIR="/run/cras" -DHAVE_SSE42=1 -DHAVE_AVX=1
//	-DHAVE_AVX2=1 -DHAVE_FMA=1 -I. -I../../src -O2 -Wall -Werror
//	-Wno-error=cpp -ffunction-sections -fdata-sections
//	-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
//	-I../../src/common -I../../src/dsp -I../../src/server
//	-I../../src/server/config -I../../src/plc -I../../src/server/rust/include
//	-I/usr/include/dbus-1.0 -I/usr/lib/x86_64-linux-gnu/dbus-1.0/include
//	-DCRAS_DBUS -I../src/common -std=gnu11 -Werror=implicit-function-declaration
//	-g -O2 -MT common/libcrasserver_la-packet_status_logger.lo -MD -MP -MF
//	common/.deps/libcrasserver_la-packet_status_logger.Tpo -c
//	../../src/common/packet_status_logger.c -fPIC -DPIC
//	-o common/.libs/libcrasserver_la-packet_status_logger.o
type compilation struct {
	execution   *Execution // original execution
	executable  string     // The compiler executable
	output      string
	inputs      []string
	defines     []string // -D"CRAS_VERSION=1"
	includeDirs []string // -I"src/common"
	libraries   []string // -l"asound"
	linkArgs    []string // "-Wl,--as-needed"
	extraFlags  []string // uninteresting flags
}

func parseCompilation(e Execution) *compilation {
	c := &compilation{
		executable: e.Executable,
		execution:  &e,
	}
	p := newParser(e)
	for !p.done() {
		if !strings.HasPrefix(p.get(), "-") {
			c.inputs = append(c.inputs, p.consume())
		} else if define, found := p.consumeShortFlag("-D"); found {
			c.defines = append(c.defines, define)
		} else if includeDir, found := p.consumeShortFlag("-I"); found {
			c.includeDirs = append(c.includeDirs, includeDir)
		} else if output, found := p.consumeShortFlag("-o"); found {
			c.output = output
		} else if lib, found := p.consumeShortFlag("-l"); found {
			c.libraries = append(c.libraries, lib)
		} else if flags, found := p.consumeExtra(); found {
			c.extraFlags = append(c.extraFlags, flags...)
		} else if p.consumeDependency() {
			// Do nothing.
		} else if strings.HasPrefix(p.get(), "-Wl,") {
			c.linkArgs = append(c.linkArgs, p.consume())
		} else {
			log.Panicf("%+v", p.args[p.i:])
		}
	}
	return c
}

func (c *compilation) flags() []string {
	var result []string
	for _, include := range c.includeDirs {
		result = append(result, "-I"+include)
	}
	for _, define := range c.defines {
		result = append(result, "-D"+define)
	}
	return append(result, c.extraFlags...)
}

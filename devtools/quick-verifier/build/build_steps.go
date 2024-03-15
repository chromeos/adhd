// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/quick-verifier/buildplan"
	"cloud.google.com/go/cloudbuild/apiv1/v2/cloudbuildpb"
	"google.golang.org/protobuf/types/known/durationpb"
)

func (cl *gerritCL) makeQuickVerifierBuild(name string) *cloudbuildpb.Build {
	return makeBuild(cl.checkoutSteps(), cl.makeTags(name))
}

// makeBuild adds steps after sources have been checked out.
func makeBuild(gitSteps *buildplan.Sequence, tags []string) *cloudbuildpb.Build {
	var b buildplan.Build
	git := b.Add(gitSteps)

	b.Add(copgenCheckSteps().WithDep(git))
	b.Add(rustGenerateSteps().WithDep(git))

	b.Add(archlinuxSteps("archlinux-clang", "--config=local-clang").WithDep(git))
	b.Add(archlinuxSteps("archlinux-clang-asan", "--config=local-clang", "--config=asan").WithDep(git))
	b.Add(archlinuxSteps("archlinux-clang-ubsan", "--config=local-clang", "--config=ubsan").WithDep(git))
	b.Add(archlinuxSteps("archlinux-gcc", "--config=local-gcc", "--config=gcc-strict").WithDep(git))
	b.Add(systemCrasRustSteps().WithDep(git))
	b.Add(kytheSteps().WithDep(git))

	ossFuzzSetup := b.Add(ossFuzzSetupSteps().WithDep(git))
	b.Add(ossFuzzSteps("oss-fuzz-address", "address", "libfuzzer").WithDep(ossFuzzSetup))
	b.Add(ossFuzzSteps("oss-fuzz-address-afl", "address", "afl").WithDep(ossFuzzSetup))
	b.Add(ossFuzzSteps("oss-fuzz-memory", "memory", "libfuzzer").WithDep(ossFuzzSetup))
	b.Add(ossFuzzSteps("oss-fuzz-undefined", "undefined", "libfuzzer").WithDep(ossFuzzSetup))
	// TODO(b/325995661): Figure out why it's broken.
	// b.Add(ossFuzzSteps("oss-fuzz-coverage", "coverage", "libfuzzer").WithDep(ossFuzzSetup))

	b.Add(cppcheckSteps().WithDep(git))

	return &cloudbuildpb.Build{
		Steps: b.AsCloudBuild(),
		Timeout: &durationpb.Duration{
			Seconds: 1200,
		},
		Tags: tags,
		Options: &cloudbuildpb.BuildOptions{
			MachineType: cloudbuildpb.BuildOptions_E2_HIGHCPU_32,
		},
	}
}

// checkoutSteps checks out the source to /workspace/adhd.
func (cl *gerritCL) checkoutSteps() *buildplan.Sequence {
	return buildplan.Commands(
		"git",
		[]*buildplan.Step{
			{
				Name:       "gcr.io/cloud-builders/git",
				Entrypoint: "git",
				Args:       []string{"clone", "--depth=1", gitURL, "adhd"},
			},
			{
				Name:       "gcr.io/cloud-builders/git",
				Entrypoint: "git",
				Args:       []string{"fetch", gitURL, cl.ref},
				Dir:        "adhd",
			},
			{
				Name:       "gcr.io/cloud-builders/git",
				Entrypoint: "git",
				Args:       []string{"checkout", "FETCH_HEAD"},
				Dir:        "adhd",
			},
		}...,
	)
}

func copMoveSourceSteps() *buildplan.Sequence {
	// CoP places sources under /workspace.
	// Move them to /workspace/adhd.
	return buildplan.Commands(
		"prepare-source",
		buildplan.Command("gcr.io/cloud-builders/git",
			"mkdir", "adhd"),
		buildplan.Command("gcr.io/cloud-builders/git",
			"find", ".", "-mindepth", "1", "-maxdepth", "1", "-not", "-name", "adhd",
			"-exec", "mv", "{}", "adhd", ";",
		),
	)
}

// MakeCopBuild returns a go/cros-cop compatible build.
func MakeCopBuild() *cloudbuildpb.Build {
	return makeBuild(
		copMoveSourceSteps(),
		nil,
	)
}

var prepareSourceStep = buildplan.Command(archlinuxBuilder, "rsync", "-ah", "/workspace/adhd/", "./")

func archlinuxSteps(id string, bazelArgs ...string) *buildplan.Sequence {
	return buildplan.Commands(
		id,
		prepareSourceStep,
		&buildplan.Step{
			Name:       archlinuxBuilder,
			Entrypoint: "bazel",
			Args: append(
				[]string{"test", "//...", "--config=ci", "-c", "dbg"},
				bazelArgs...,
			),
		},
	).WithVolume()
}

func systemCrasRustSteps() *buildplan.Sequence {
	return buildplan.Commands("archlinux-system-cras-rust",
		[]*buildplan.Step{
			prepareSourceStep,
			{
				Name:       archlinuxBuilder,
				Entrypoint: "cargo",
				Args:       []string{"install", "dbus-codegen"},
			},
			{
				Name:       archlinuxBuilder,
				Entrypoint: "cargo",
				Args:       []string{"build", "--workspace"},
			},
			{
				Name:       archlinuxBuilder,
				Entrypoint: "cargo",
				Args:       []string{"test", "--workspace"},
			},
			{
				Name:       archlinuxBuilder,
				Entrypoint: "bazel",
				Args: []string{
					"test", "//...", "--config=ci", "-c", "dbg",
					"--//:system_cras_rust",
					"--config=local-clang",
					"--linkopt=-L/workspace-archlinux-system-cras-rust/target/debug",
				},
			},
		}...,
	).WithVolume()
}

func kytheSteps() *buildplan.Sequence {
	return buildplan.Commands(
		"kythe",
		prepareSourceStep,
		buildplan.Command(archlinuxBuilder, "bash", "devtools/kythe/build_kzip.bash", "."),
	).WithVolume()
}

func rustGenerateSteps() *buildplan.Sequence {
	return buildplan.Commands(
		"rust_generate",
		prepareSourceStep,
		buildplan.Command(archlinuxBuilder, "devtools/rust_generate.bash"),
		buildplan.Command(archlinuxBuilder, "git", "diff", "--exit-code"),
	)
}

func ossFuzzSetupSteps() *buildplan.Sequence {
	return buildplan.Commands(
		"oss-fuzz-setup",
		[]*buildplan.Step{
			{
				Name:       archlinuxBuilder,
				Entrypoint: "mkdir",
				Args:       []string{"oss-fuzz-setup"},
			},
			{
				Name:       archlinuxBuilder,
				Entrypoint: "rsync",
				Args:       []string{"-ah", "/workspace/adhd/", "adhd/"},
				Dir:        "oss-fuzz-setup",
			},
			{
				Name:       "gcr.io/cloud-builders/git",
				Entrypoint: "git",
				Args:       []string{"clone", "--depth=1", "https://github.com/google/oss-fuzz"},
				Dir:        "oss-fuzz-setup",
			},
			{
				Name:       "gcr.io/cloud-builders/docker",
				Entrypoint: "python3",
				Args:       []string{"oss-fuzz/infra/helper.py", "build_image", "--pull", "cras"},
				Dir:        "oss-fuzz-setup",
			},
		}...,
	)
}

func ossFuzzSteps(id, sanitizer, engine string) *buildplan.Sequence {
	var checkStep *buildplan.Step
	if sanitizer == "coverage" {
		checkStep = &buildplan.Step{
			Name:       "gcr.io/google.com/cloudsdktool/cloud-sdk",
			Entrypoint: "python3",
			Args: []string{
				"oss-fuzz/infra/helper.py", "coverage", "cras",
				"--port=", // Pass empty port to not run an HTTP server.
			},
			Dir: id,
		}
	} else {
		checkStep = &buildplan.Step{
			Name:       "gcr.io/cloud-builders/docker",
			Entrypoint: "python3",
			Args:       []string{"oss-fuzz/infra/helper.py", "check_build", "--sanitizer", sanitizer, "--engine", engine, "cras"},
			Dir:        id,
		}
	}
	return buildplan.Commands(
		id,
		buildplan.Command(archlinuxBuilder, "rsync", "-ah", "oss-fuzz-setup/", id+"/"),
		&buildplan.Step{
			Name:       "gcr.io/cloud-builders/docker",
			Entrypoint: "python3",
			Args:       []string{"oss-fuzz/infra/helper.py", "build_fuzzers", "--sanitizer", sanitizer, "--engine", engine, "cras", "/workspace/" + id + "/adhd"},
			Dir:        id,
		},
		checkStep,
	)
}

func copgenCheckSteps() *buildplan.Sequence {
	return buildplan.Commands(
		"copgen-check",
		prepareSourceStep,
		buildplan.Command(archlinuxBuilder, "devtools/copgen.sh", "--check"),
	).WithVolume()
}

func cppcheckSteps() *buildplan.Sequence {
	return buildplan.Commands("cppcheck",
		[]*buildplan.Step{
			prepareSourceStep,
			{
				Name:       archlinuxBuilder,
				Entrypoint: "bazel",
				Args: []string{
					"build", "//...",
					"--config=ci",
					"--config=local-clang",
				},
			},
			{
				Name:       archlinuxBuilder,
				Entrypoint: "bazel",
				Args: []string{
					"run", "//:compdb",
				},
			},
			{
				Name: archlinuxBuilder,
				Args: []string{
					"devtools/cppcheck.sh", "/workspace-cppcheck/compile_commands.json",
				},
			},
		}...,
	).WithVolume()
}

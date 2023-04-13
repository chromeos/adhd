// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"net/url"
	"strconv"
	"strings"

	"chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/quick-verifier/buildplan"
	"chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/quick-verifier/wc"
	cloudbuild "cloud.google.com/go/cloudbuild/apiv1"
	"cloud.google.com/go/cloudbuild/apiv1/v2/cloudbuildpb"
	"github.com/andygrunwald/go-gerrit"
	"google.golang.org/api/iterator"
	"google.golang.org/protobuf/types/known/durationpb"
)

const (
	projectID        = "chromeos-audio-qv"
	archlinuxBuilder = "gcr.io/" + projectID + "/archlinux-builder"
	gitURL           = "https://chromium.googlesource.com/chromiumos/third_party/adhd"
)

type gerritCL struct {
	changeID   int
	revisionID int
	ref        string
}

func (cl *gerritCL) url() string {
	return fmt.Sprintf("https://crrev.com/c/%d/%d", cl.changeID, cl.revisionID)
}

func (cl *gerritCL) clTag() string {
	return fmt.Sprintf("c-%d-%d", cl.changeID, cl.revisionID)
}

func (cl *gerritCL) makeTags(name string) []string {
	return []string{
		// "manual",
		cl.clTag(),
		name,
	}
}

func (cl *gerritCL) builds() []*cloudbuildpb.Build {
	return []*cloudbuildpb.Build{
		cl.makeBuild("default"),
	}
}

func (cl *gerritCL) makeBuild(name string) *cloudbuildpb.Build {
	var b buildplan.Build
	git := b.Add(cl.checkoutSteps())

	b.Add(archlinuxSteps("archlinux-clang", "--config=local-clang").WithDep(git))
	b.Add(archlinuxSteps("archlinux-clang-asan", "--config=local-clang", "--config=asan").WithDep(git))
	b.Add(archlinuxSteps("archlinux-clang-ubsan", "--config=local-clang", "--config=ubsan").WithDep(git))
	b.Add(archlinuxSteps("archlinux-gcc", "--config=local-gcc").WithDep(git))
	b.Add(systemCrasRustSteps().WithDep(git))
	b.Add(kytheSteps().WithDep(git))

	ossFuzzSetup := b.Add(ossFuzzSetupSteps().WithDep(git))
	b.Add(ossFuzzSteps("oss-fuzz-address", "address", "libfuzzer").WithDep(ossFuzzSetup))
	b.Add(ossFuzzSteps("oss-fuzz-address-afl", "address", "afl").WithDep(ossFuzzSetup))
	b.Add(ossFuzzSteps("oss-fuzz-memory", "memory", "libfuzzer").WithDep(ossFuzzSetup))
	b.Add(ossFuzzSteps("oss-fuzz-undefined", "undefined", "libfuzzer").WithDep(ossFuzzSetup))
	b.Add(ossFuzzSteps("oss-fuzz-coverage", "coverage", "libfuzzer").WithDep(ossFuzzSetup))

	return &cloudbuildpb.Build{
		Steps: b.AsCloudBuild(),
		Timeout: &durationpb.Duration{
			Seconds: 1200,
		},
		Tags: cl.makeTags(name),
		Options: &cloudbuildpb.BuildOptions{
			MachineType: cloudbuildpb.BuildOptions_E2_HIGHCPU_32,
		},
	}
}

func (cl *gerritCL) checkoutSteps() *buildplan.Sequence {
	return buildplan.Commands(
		"git",
		[]*buildplan.Step{
			{
				Name:       "gcr.io/cloud-builders/git",
				Entrypoint: "git",
				Args:       []string{"clone", "--depth=1", gitURL, "."},
			},
			{
				Name:       "gcr.io/cloud-builders/git",
				Entrypoint: "git",
				Args:       []string{"fetch", gitURL, cl.ref},
			},
			{
				Name:       "gcr.io/cloud-builders/git",
				Entrypoint: "git",
				Args:       []string{"checkout", "FETCH_HEAD"},
			},
		}...,
	)
}

var prepareSourceStep = buildplan.Command(archlinuxBuilder, "rsync", "-ah", "/workspace/", "./")

func archlinuxSteps(id string, bazelArgs ...string) *buildplan.Sequence {
	return buildplan.Commands(
		id,
		prepareSourceStep,
		&buildplan.Step{
			Name:       archlinuxBuilder,
			Entrypoint: "bazel",
			Args: append(
				[]string{"test", "//...", "-k", "-c", "dbg", "--test_output=errors"},
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
					"test", "//...", "-k", "-c", "dbg", "--test_output=errors",
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
		buildplan.Command(archlinuxBuilder, "bash", "/build_kzip.bash", "."),
	).WithVolume()
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
				Args:       []string{"-ah", "/workspace/", "adhd/"},
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

func submit(cl gerritCL, triggerID string) error {
	ctx := context.Background()

	gerritClient, err := gerrit.NewClient("https://chromium-review.googlesource.com", nil)
	if err != nil {
		log.Fatal("Cannot create gerrit client: ", err)
	}
	changeInfo, _, err := gerritClient.Changes.GetReview(strconv.Itoa(cl.changeID), strconv.Itoa(cl.revisionID))
	if err != nil {
		log.Fatal("Cannot get review: ", err)
	}
	for _, revision := range changeInfo.Revisions {
		if revision.Number == cl.revisionID {
			cl.ref = revision.Ref
			break
		}
	}
	if cl.ref == "" {
		log.Fatalf("Cannot find revision: %d", cl.revisionID)
	}

	c, err := cloudbuild.NewClient(ctx)
	if err != nil {
		return fmt.Errorf("cloudbuild.NewClient: %v", err)
	}
	defer c.Close()

	for _, build := range cl.builds() {
		build.Tags = append(build.Tags, triggerID)
		req := &cloudbuildpb.CreateBuildRequest{
			ProjectId: projectID,
			Build:     build,
		}
		if err := buildCreateLimiter.Wait(ctx); err != nil {
			return fmt.Errorf("buildCreateLimiter.Wait(): %w", err)
		}
		_, err = c.CreateBuild(ctx, req)
		if err != nil {
			return fmt.Errorf("c.CreateBuild: %v", err)
		}
	}

	return err
}

func buildURL(triggerID string) string {
	return (&url.URL{
		Scheme: "https",
		Host:   "console.cloud.google.com",
		Path:   "cloud-build/builds",
		RawQuery: url.Values{
			"project": []string{projectID},
			"query":   []string{fmt.Sprintf("tags=%s", triggerID)},
		}.Encode(),
	}).String()
}

type buildResult struct {
	triggerID     string
	succeedBuilds []string
	failedBuilds  []string
	knownFailures []string
}

func (r *buildResult) String() string {
	w := &strings.Builder{}
	fmt.Fprintf(w, "%d/%d builds passed\n", len(r.succeedBuilds), len(r.succeedBuilds)+len(r.failedBuilds)+len(r.knownFailures))
	if len(r.failedBuilds) > 0 {
		fmt.Fprintf(w, "%d failed builds: %s\n", len(r.failedBuilds), strings.Join(r.failedBuilds, ", "))
	}
	if len(r.knownFailures) > 0 {
		fmt.Fprintf(w, "%d failed non-critical builds: %s\n", len(r.knownFailures), strings.Join(r.knownFailures, ", "))
	}
	fmt.Fprintf(w, "Logs: %s\n", buildURL(r.triggerID))
	return w.String()
}

func (r *buildResult) ok() bool {
	return len(r.failedBuilds) == 0
}

var (
	running = errors.New("build is still running")
)

func buildName(build *cloudbuildpb.Build) string {
	for _, tag := range build.Tags {
		if tag == "manual" {
			continue
		}
		if strings.HasPrefix(tag, "c-") {
			continue
		}
		return tag
	}
	return fmt.Sprintf("<unknown build %s>", build.Id)
}

var knownFailures = map[string]bool{
	"archlinux-tsan": true,
	// "oss-fuzz-coverage": true,
}

func queryBuilds(triggerID string) (*buildResult, error) {
	ctx := context.Background()
	c, err := cloudbuild.NewClient(ctx)
	if err != nil {
		return nil, fmt.Errorf("cloudbuild.NewClient: %v", err)
	}
	defer c.Close()

	result := buildResult{
		triggerID: triggerID,
	}
	buildIt := c.ListBuilds(ctx, &cloudbuildpb.ListBuildsRequest{
		ProjectId: projectID,
		Filter:    fmt.Sprintf("tags=%s", triggerID),
	})
	for {
		build, err := buildIt.Next()
		if err == iterator.Done {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("Unexpected error iterating builds: %w", err)
		}

		name := buildName(build)
		switch build.Status {
		case cloudbuildpb.Build_PENDING, cloudbuildpb.Build_QUEUED, cloudbuildpb.Build_WORKING:
			return nil, running
		case cloudbuildpb.Build_SUCCESS:
			result.succeedBuilds = append(result.succeedBuilds, name)
		case cloudbuildpb.Build_CANCELLED, cloudbuildpb.Build_FAILURE, cloudbuildpb.Build_EXPIRED,
			cloudbuildpb.Build_TIMEOUT, cloudbuildpb.Build_INTERNAL_ERROR:
			if knownFailures[name] {
				result.knownFailures = append(result.knownFailures, name)
			} else {
				result.failedBuilds = append(result.failedBuilds, name)
			}
		default:
			return nil, fmt.Errorf("Unexpected build status %s", build.Status)
		}
	}

	return &result, nil
}

const (
	query = ("project:chromiumos/third_party/adhd" +
		" branch:main" +
		" is:open" +
		" after:2023-02-08" + // Ignore old CLs.
		" -hashtag:audio-qv-ignore" + // User request to ignore.
		" (uploaderin:chromeos-gerrit-sandbox-access OR label:Code-Owners=ok)" + // Only handle "trusted" CLs.
		" ((-is:wip -label:Verified=ANY,user=1571002) OR hashtag:audio-qv-trigger)" + // Only handle open CLs that are not voted.
		"")
	audioQVTrigger = "audio-qv-trigger"
	botID          = 1571002
)

type state int

const (
	newState       = iota // The CL has not been handled.
	triggeredState        // The CL has a trigger ID associated.
	completedState        // The CL is completed.
)

func main() {
	log.SetFlags(log.Ltime | log.Lshortfile)

	gerritClient, err := wc.NewGerritClient("https://chromium-review.googlesource.com")
	if err != nil {
		log.Fatal("Cannot create gerrit client: ", err)
	}

	changesPtr, _, err := gerritClient.Changes.QueryChanges(&gerrit.QueryChangeOptions{
		QueryOptions: gerrit.QueryOptions{
			Query: []string{query},
		},
		ChangeOptions: gerrit.ChangeOptions{
			AdditionalFields: []string{"CURRENT_REVISION", "MESSAGES"},
		},
	})
	if err != nil {
		log.Fatal(err)
	}

	for _, change := range *changesPtr {
		processChange(gerritClient, change)
	}
}

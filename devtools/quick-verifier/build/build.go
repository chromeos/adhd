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
		cl.makeBuild("archlinux", "--config=local-clang"),
		cl.makeBuild("archlinux-asan", "--config=local-clang", "--config=asan"),
		cl.makeBuild("archlinux-ubasan", "--config=local-clang", "--config=ubsan"),
		// cl.makeBuild("archlinux-tsan", "--config=local-clang", "--config=tsan"),
		cl.makeBuild("archlinux-gcc", "--config=local-gcc"),
		cl.makeSystemCrasRustBuild("archlinux-system_cras_rust"),
		cl.makeKytheBuild("kythe"),

		cl.makeOssFuzzBuild("oss-fuzz-address", "address", "libfuzzer"),
		cl.makeOssFuzzBuild("oss-fuzz-address-afl", "address", "afl"),
		cl.makeOssFuzzBuild("oss-fuzz-memory", "memory", "libfuzzer"),
		cl.makeOssFuzzBuild("oss-fuzz-undefined", "undefined", "libfuzzer"),
		cl.makeOssFuzzBuild("oss-fuzz-coverage", "coverage", "libfuzzer"),

		// cl.makeDevtoolsBuild("archlinux-devtools"),
	}
}

func (cl *gerritCL) checkoutSteps() []*cloudbuildpb.BuildStep {
	return []*cloudbuildpb.BuildStep{
		{
			Name:       "gcr.io/cloud-builders/git",
			Entrypoint: "git",
			Args:       []string{"clone", gitURL},
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
	}
}

func (cl *gerritCL) makeBuild(name string, bazelArgs ...string) *cloudbuildpb.Build {
	return &cloudbuildpb.Build{
		Steps: append(cl.checkoutSteps(), []*cloudbuildpb.BuildStep{
			{
				Name:       archlinuxBuilder,
				Entrypoint: "bazel",
				Args: append(
					[]string{"test", "//...", "-k", "-c", "dbg", "--test_output=errors"},
					bazelArgs...,
				),
				Dir: "adhd",
			},
		}...),
		Timeout: &durationpb.Duration{
			Seconds: 1200,
		},
		Tags: cl.makeTags(name),
		Options: &cloudbuildpb.BuildOptions{
			MachineType: cloudbuildpb.BuildOptions_E2_HIGHCPU_8,
		},
	}
}

func (cl *gerritCL) makeKytheBuild(name string) *cloudbuildpb.Build {
	return &cloudbuildpb.Build{
		Steps: append(cl.checkoutSteps(), []*cloudbuildpb.BuildStep{
			{
				Name:       archlinuxBuilder,
				Entrypoint: "bash",
				Args:       []string{"/build_kzip.bash"},
			},
		}...),
		Timeout: &durationpb.Duration{
			Seconds: 1200,
		},
		Tags: cl.makeTags(name),
		Options: &cloudbuildpb.BuildOptions{
			MachineType: cloudbuildpb.BuildOptions_E2_HIGHCPU_8,
		},
	}
}

func (cl *gerritCL) makeSystemCrasRustBuild(name string) *cloudbuildpb.Build {
	return &cloudbuildpb.Build{
		Steps: append(cl.checkoutSteps(), []*cloudbuildpb.BuildStep{
			{
				Name:       archlinuxBuilder,
				Entrypoint: "cargo",
				Args:       []string{"install", "dbus-codegen"},
			},
			{
				Name:       archlinuxBuilder,
				Entrypoint: "cargo",
				Args:       []string{"build", "--workspace"},
				// TODO(b/274360274): Run in adhd.
				Dir: "adhd/cras/src/server/rust",
			},
			{
				Name:       archlinuxBuilder,
				Entrypoint: "cargo",
				Args:       []string{"test", "--workspace"},
				// TODO(b/274360274): Run in adhd.
				Dir: "adhd/cras/src/server/rust",
			},
			{
				Name:       archlinuxBuilder,
				Entrypoint: "bazel",
				Args: []string{
					"test", "//...", "-k", "-c", "dbg", "--test_output=errors",
					"--//:system_cras_rust",
					"--config=local-clang",
					"--linkopt=-L/workspace/adhd/target/debug",
					// TODO(b/274360274): Remove this --linkopt.
					"--linkopt=-L/workspace/adhd/cras/src/server/rust/target/debug",
				},
				Dir: "adhd",
			},
		}...),
		Timeout: &durationpb.Duration{
			Seconds: 1200,
		},
		Tags: cl.makeTags(name),
		Options: &cloudbuildpb.BuildOptions{
			MachineType: cloudbuildpb.BuildOptions_E2_HIGHCPU_8,
		},
	}
}

func (cl *gerritCL) makeDevtoolsBuild(name string) *cloudbuildpb.Build {
	return &cloudbuildpb.Build{
		Steps: append(cl.checkoutSteps(), []*cloudbuildpb.BuildStep{
			{
				Name:       archlinuxBuilder,
				Entrypoint: "bazel",
				Args:       []string{"test", "//...", "--test_output=errors"},
				Dir:        "adhd/devtools",
			},
		}...),
		Tags: cl.makeTags(name),
		Options: &cloudbuildpb.BuildOptions{
			// TODO: Cache build artifacts so we don't need a beefy machine.
			MachineType: cloudbuildpb.BuildOptions_E2_HIGHCPU_32,
		},
	}
}

func (cl *gerritCL) makeOssFuzzBuild(name, sanitizer, engine string) *cloudbuildpb.Build {
	var checkStep *cloudbuildpb.BuildStep
	if sanitizer == "coverage" {
		checkStep = &cloudbuildpb.BuildStep{
			Name:       "gcr.io/google.com/cloudsdktool/cloud-sdk",
			Entrypoint: "python3",
			Args: []string{
				"oss-fuzz/infra/helper.py", "coverage", "cras",
				"--port=", // Pass empty port to not run an HTTP server.
			},
		}
	} else {
		checkStep = &cloudbuildpb.BuildStep{
			Name:       "gcr.io/cloud-builders/docker",
			Entrypoint: "python3",
			Args:       []string{"oss-fuzz/infra/helper.py", "check_build", "--sanitizer", sanitizer, "--engine", engine, "cras"},
		}
	}
	return &cloudbuildpb.Build{
		Steps: append(cl.checkoutSteps(), []*cloudbuildpb.BuildStep{
			{
				Name:       "gcr.io/cloud-builders/git",
				Entrypoint: "git",
				Args:       []string{"clone", "https://github.com/google/oss-fuzz"},
			},
			{
				Name:       "gcr.io/cloud-builders/docker",
				Entrypoint: "python3",
				Args:       []string{"oss-fuzz/infra/helper.py", "build_image", "--pull", "cras"},
			},
			{
				Name:       "gcr.io/cloud-builders/docker",
				Entrypoint: "python3",
				Args:       []string{"oss-fuzz/infra/helper.py", "build_fuzzers", "--sanitizer", sanitizer, "--engine", engine, "cras", "/workspace/adhd"},
			},
			checkStep,
		}...),
		Timeout: &durationpb.Duration{
			Seconds: 1200,
		},
		Tags: cl.makeTags(name),
		Options: &cloudbuildpb.BuildOptions{
			MachineType: cloudbuildpb.BuildOptions_E2_HIGHCPU_8,
		},
	}
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

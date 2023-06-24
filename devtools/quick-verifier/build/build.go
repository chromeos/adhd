// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"context"
	"errors"
	"fmt"
	"log"
	"net/url"
	"strconv"
	"strings"

	cloudbuild "cloud.google.com/go/cloudbuild/apiv1"
	"cloud.google.com/go/cloudbuild/apiv1/v2/cloudbuildpb"
	"github.com/andygrunwald/go-gerrit"
	"google.golang.org/api/iterator"
)

const (
	projectID        = "chromeos-audio-qv"
	archlinuxBuilder = "gcr.io/${PROJECT_ID}/adhd-archlinux-builder"
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
		cl.makeQuickVerifierBuild("default"),
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
	extraInfo     string
}

func (r *buildResult) String() string {
	w := &strings.Builder{}
	fmt.Fprintf(w, "%d/%d builds passed\n", len(r.succeedBuilds), len(r.succeedBuilds)+len(r.failedBuilds))
	if len(r.failedBuilds) > 0 {
		fmt.Fprintf(w, "%d failed builds: %s\n", len(r.failedBuilds), strings.Join(r.failedBuilds, ", "))
	}
	fmt.Fprintf(w, "Logs: %s\n", buildURL(r.triggerID))
	fmt.Fprintf(w, r.extraInfo)
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

func buildDiagnostics(build *cloudbuildpb.Build) string {
	var w strings.Builder
	for i, step := range build.Steps {
		switch step.GetStatus() {
		case cloudbuildpb.Build_SUCCESS:
			continue
		case cloudbuildpb.Build_CANCELLED, cloudbuildpb.Build_QUEUED:
			// Interrutped due to other failures.
			continue
		}
		id := step.GetId()
		if id == "" {
			id = fmt.Sprintf("unnamed-step-%d", i)
		}
		url, err := url.Parse(build.LogUrl)
		if err != nil {
			log.Panicf("bad url %q", url)
		}
		url.Path = fmt.Sprintf("%s;step=%d", url.Path, i)
		fmt.Fprintf(&w, "* Step [%s](%s) of build %s ended with status: %s\n", id, url, buildName(build), build.Status)
	}
	return w.String()
}

func buildStatus(build *cloudbuildpb.Build) (passed bool, diagnostics string, err error) {
	switch build.Status {
	case cloudbuildpb.Build_PENDING, cloudbuildpb.Build_QUEUED, cloudbuildpb.Build_WORKING:
		return false, "", running
	case cloudbuildpb.Build_SUCCESS:
		return true, "", nil
	case cloudbuildpb.Build_CANCELLED, cloudbuildpb.Build_FAILURE, cloudbuildpb.Build_EXPIRED,
		cloudbuildpb.Build_TIMEOUT, cloudbuildpb.Build_INTERNAL_ERROR:
		return false, buildDiagnostics(build), nil
	default:
		return false, "", fmt.Errorf("unexpected build status %s", build.Status)
	}
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
	var extraInfoBuilder strings.Builder
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
			return nil, fmt.Errorf("unexpected error iterating builds: %w", err)
		}
		name := buildName(build)
		passed, diags, err := buildStatus(build)
		if err != nil {
			return nil, err
		}
		if passed {
			result.succeedBuilds = append(result.succeedBuilds, name)
		} else {
			result.failedBuilds = append(result.failedBuilds, name)
			extraInfoBuilder.WriteString(diags)
		}
	}
	result.extraInfo = extraInfoBuilder.String()
	return &result, nil
}

const (
	audioQVTrigger = "audio-qv-trigger"
	botID          = 1571002
)

type state int

const (
	newState       = iota // The CL has not been handled.
	triggeredState        // The CL has a trigger ID associated.
	completedState        // The CL is completed.
)

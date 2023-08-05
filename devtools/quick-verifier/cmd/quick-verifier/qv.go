// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"errors"
	"log"

	"chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/quick-verifier/build"
	"chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/quick-verifier/wc"
	"cloud.google.com/go/compute/metadata"
	"github.com/andygrunwald/go-gerrit"
)

const query = ("project:chromiumos/third_party/adhd" +
	" branch:main" +
	" is:open" +
	" after:2023-02-08" + // Ignore old CLs.
	" -hashtag:audio-qv-ignore" + // User request to ignore.
	" (uploaderin:chromeos-gerrit-sandbox-access OR label:Code-Owners=ok)" + // Only handle "trusted" CLs.
	" ((-is:wip -label:Verified=ANY,user=1571002) OR hashtag:audio-qv-trigger)" + // Only handle open CLs that are not voted.
	"")

func getGerritClient() (*gerrit.Client, error) {
	const url = "https://chromium-review.googlesource.com"

	if wc.HasGitCookies() {
		return wc.NewGerritClient(url, wc.FsCookieJar{})
	}
	if metadata.OnGCE() {
		return wc.NewGerritClient(url, wc.NewGCECookieJar())
	}
	return nil, errors.New("missing gerrit gitcookies and not on GCE")
}

func main() {
	log.SetFlags(log.Ltime | log.Lshortfile)

	gerritClient, err := getGerritClient()
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
		build.ProcessChange(gerritClient, change)
	}
}

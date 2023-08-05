// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cloudfunc

import (
	"context"
	"errors"
	"fmt"
	"sync"

	"chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/quick-verifier/build"
	"chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/quick-verifier/qv"
	"chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/quick-verifier/wc"
	"github.com/andygrunwald/go-gerrit"
)

var mux sync.Mutex

func Handle(ctx context.Context, event struct{}) error {
	if !mux.TryLock() {
		return errors.New("failed to acquire mutex")
	}
	defer mux.Unlock()

	gerritClient, err := wc.NewGerritClient(
		"https://chromium-review.googlesource.com",
		wc.NewGCECookieJar(),
	)
	if err != nil {
		return fmt.Errorf("cannot create gerrit client: %w", err)
	}

	changesPtr, _, err := gerritClient.Changes.QueryChanges(&gerrit.QueryChangeOptions{
		QueryOptions: gerrit.QueryOptions{
			Query: []string{qv.Query},
		},
		ChangeOptions: gerrit.ChangeOptions{
			AdditionalFields: []string{"CURRENT_REVISION", "MESSAGES"},
		},
	})
	if err != nil {
		return fmt.Errorf("cannot query changes: %w", err)
	}

	for _, change := range *changesPtr {
		build.ProcessChange(gerritClient, change)
	}

	return nil
}

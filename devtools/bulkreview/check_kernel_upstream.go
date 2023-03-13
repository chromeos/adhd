// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bulkreview

import (
	"strings"

	"github.com/andygrunwald/go-gerrit"
	"github.com/sirupsen/logrus"
)

// Check if the CL has
// Return the checked CL with revision information, and a bool indicating
// whether it has:
//
//	No changes have been detected between this change and its upstream source!
//
// from sean@poorly.run.
func CheckKernelUpstream(c *Client, cl Change) (Change, bool) {
	detail, _, err := c.gerritClients[cl.GerritHost].Changes.GetChangeDetail(
		cl.ID,
		&gerrit.ChangeOptions{
			AdditionalFields: []string{
				"CURRENT_REVISION",
				"MESSAGES",
			},
		},
	)
	if err != nil {
		logrus.Fatal(err)
	}

	cl.Revision = detail.Revisions[detail.CurrentRevision].Number
	for _, message := range detail.Messages {
		if message.Author.AccountID != 1546357 {
			// Not sean@poorly.run.
			continue
		}
		if message.RevisionNumber != cl.Revision {
			continue
		}
		if strings.Contains(
			message.Message,
			"No changes have been detected between this change and its upstream source!",
		) {
			return cl, true
		}
	}
	return cl, false
}

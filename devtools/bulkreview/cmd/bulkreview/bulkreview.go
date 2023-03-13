// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/andygrunwald/go-gerrit"
	"github.com/sirupsen/logrus"
	"github.com/spf13/pflag"

	bulkreview "chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/bulkreview"
	"chromium.googlesource.com/chromiumos/third_party/adhd.git/devtools/bulkreview/labels"
)

const (
	ignoreLabel = 1000
)

func main() {
	cq := pflag.Int("cq", ignoreLabel, "set Commit-Queue vote")
	cr := pflag.Int("cr", ignoreLabel, "set Code-Review vote")
	v := pflag.Int("v", ignoreLabel, "set Verified vote")

	reviewMessage := pflag.String("review-message", "", "code review message")

	followCqDepend := pflag.BoolP("cq-depend", "d", false, "follow Cq-Depend recursively")
	followRelationChain := pflag.BoolP("relation", "r", false, "follow relation chain")

	checkKernelUpstream := pflag.Bool("check-kernel-upstream", false,
		`Check for "No changes have been detected between this change and its upstream source!" by sean@poorly.run`,
	)

	removeAttention := pflag.Bool("remove-attention", false,
		"remove the user from the attention set")
	attentionUser := pflag.String("attention-user", bulkreview.UserSelf,
		"the user to remove attention from, "+
			"if --remove-attention is specified, "+
			"defaults to self",
	)

	removeReviewer := pflag.String("remove-reviewer", "", "remove the user from the list of reviewers")

	printTryjobCommand := pflag.Bool("print-tryjob-command", false, "Print cros tryjob command including the CLs")
	print := pflag.Bool("print", false, "Print the CLs to stdout")

	stalk := pflag.Bool("stalk", false, "perform actions on even ABANDONED or MERGED CLs")

	pflag.Parse()

	c, err := bulkreview.NewClient()
	if err != nil {
		logrus.Fatal(err)
	}

	var changes []bulkreview.Change
	for _, arg := range pflag.Args() {
		cl, err := bulkreview.ParseChange(arg)
		if err != nil {
			logrus.Fatal(err)
		}
		changes = append(changes, cl)
	}
	logrus.Printf("looking for dependencies of %s", changes)
	changes, err = bulkreview.FindDependencies(
		bulkreview.DependencyResolverOptions{
			C:                   c,
			FollowRelationChain: *followRelationChain,
			FollowCqDepend:      *followCqDepend,
			Stalk:               *stalk,
		},
		changes...,
	)
	if err != nil {
		logrus.Fatal(err)
	}

	logrus.Println(len(changes), "changes to consider")

	if *checkKernelUpstream {
		var checkedCLs []bulkreview.Change
		allUpstream := true
		logrus.Println("Checking kernel upstream...")
		for i, cl := range changes {
			ccl, ok := bulkreview.CheckKernelUpstream(c, cl)
			checkedCLs = append(checkedCLs, ccl)
			if ok {
				logrus.Printf("%d/%d %s OK", i+1, len(changes), ccl)
			} else {
				logrus.Printf("%d/%d %s Missing", i+1, len(changes), ccl)
				allUpstream = false
			}
		}
		if !allUpstream {
			logrus.Fatal("Not all CLs are upstream")
		}
		changes = checkedCLs
	}

	votes := map[string]string{}
	if *cq != ignoreLabel {
		votes[labels.CommitQueue] = strconv.Itoa(*cq)
	}
	if *cr != ignoreLabel {
		votes[labels.CodeReview] = strconv.Itoa(*cr)
	}
	if *v != ignoreLabel {
		votes[labels.Verified] = strconv.Itoa(*v)
	}

	for i, cl := range changes {
		logrus.Printf("(%d/%d) %s", i+1, len(changes), cl)

		cg := c.Host(cl.GerritHost)

		if len(votes) > 0 || *reviewMessage != "" {
			review := &gerrit.ReviewInput{
				Labels:  votes,
				Message: *reviewMessage,
			}
			revision := bulkreview.RevisionCurrent
			if cl.Revision != 0 {
				revision = strconv.Itoa(cl.Revision)
			}
			r, _, err := cg.Changes.SetReview(cl.ID, revision, review)
			if err != nil {
				logrus.Fatal(err)
			}
			logrus.Printf("%+v", r)
		}

		if *removeAttention {
			_, err := cg.Changes.RemoveAttention(
				cl.ID,
				*attentionUser,
				&gerrit.AttentionSetInput{
					Reason: fmt.Sprintf("bulkreview --remove-attention=%s", *attentionUser),
				},
			)
			if err != nil {
				logrus.Fatal(err)
			}
			logrus.Infof("Removed %s from attention set", *attentionUser)
		}

		if *removeReviewer != "" {
			account, _, err := cg.Accounts.GetAccount(*removeReviewer)
			if err != nil {
				logrus.Error("cannot get account:", *removeReviewer)
			}
			reviewers, _, err := cg.Changes.ListReviewers(
				cl.ID,
			)
			if err != nil {
				logrus.Error(err)
			}
			for _, reviewer := range *reviewers {
				if reviewer.AccountID == account.AccountID {
					logrus.Infof("Removing reviewer %s", account.Email)
					cg.Changes.DeleteReviewer(cl.ID, account.Email)
				}
			}
		}
	}

	if *printTryjobCommand {
		command := &strings.Builder{}
		command.WriteString("cros tryjob")
		for _, cl := range changes {
			fmt.Fprintf(command, " -g '%s'", cl.CrosTryjobID())
		}
		fmt.Println(command)
	}
	if *print {
		for _, cl := range changes {
			fmt.Println(cl)
		}
	}
}

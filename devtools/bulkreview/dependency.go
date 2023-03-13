// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bulkreview

import (
	"strconv"
	"strings"

	"github.com/andygrunwald/go-gerrit"
	"github.com/sirupsen/logrus"
)

type dependencyResolver struct {
	DependencyResolverOptions
	// state
	cls     []Change
	visited map[Change]bool
}

type DependencyResolverOptions struct {
	C                   *Client
	FollowRelationChain bool
	FollowCqDepend      bool
	Stalk               bool
}

func (d *dependencyResolver) visit(cl Change, followRelationChain, checkOpen bool) (err error) {
	if d.visited[cl] {
		return nil
	}
	d.visited[cl] = true

	if checkOpen && !d.Stalk {
		change, _, err := d.C.Host(cl.GerritHost).Changes.GetChange(cl.ID, nil)
		if err != nil {
			return err
		}
		if !statusIsOpen(change.Status) {
			logrus.Infof("%s is %s, not proceeding", cl, change.Status)
			return nil
		}
	}
	d.cls = append(d.cls, cl)
	if d.FollowRelationChain && followRelationChain {
		err = d.visitRelatedChanges(cl)
		if err != nil {
			return err
		}
	}

	if d.FollowCqDepend {
		err = d.visitCqDepends(cl)
	}

	return
}

func (d *dependencyResolver) visitRelatedChanges(cl Change) error {
	logrus.Infof("checking %s for relation chain", cl)

	gc := d.C.Host(cl.GerritHost)

	cls, _, err := gc.Changes.GetRelatedChanges(cl.ID, RevisionCurrent)
	if err != nil {
		return err
	}

	foundParent := false
	for _, child := range cls.Changes {
		if foundParent {
			if !statusIsOpen(child.Status) && !d.Stalk {
				logrus.Infof("%s is %s, not proceeding with relation chain", cl, child.Status)
				break
			}

			err = d.visit(
				Change{cl.GerritHost, strconv.Itoa(child.ChangeNumber), 0},
				false, // already on relation chain, don't follow
				false,
			)
			if err != nil {
				return err
			}
		} else if cl.ID == strconv.Itoa(child.ChangeNumber) ||
			cl.ID == child.ChangeID {
			foundParent = true
		}
	}

	return nil
}

func (d *dependencyResolver) visitCqDepends(cl Change) (err error) {
	logrus.Infof("checking %s for Cq-Depend", cl)

	gc := d.C.Host(cl.GerritHost)

	commit, _, err := gc.Changes.GetCommit(
		cl.ID, RevisionCurrent,
		&gerrit.CommitOptions{Weblinks: true},
	)
	if err != nil {
		return err
	}

	for _, child := range parseCqDepend(commit.Message) {
		err = d.visit(
			child,
			true, // follow branched relation chain
			true,
		)
		if err != nil {
			return err
		}
	}

	return nil
}

func parseCqDepend(commitMessage string) []Change {
	var cls []Change
	for _, line := range strings.Split(commitMessage, "\n") {
		const cqDependLabel = "Cq-Depend:"
		if strings.HasPrefix(line, cqDependLabel) {
			for _, s := range strings.Split(line[len(cqDependLabel):], ",") {
				cl, err := ParseChange(strings.TrimSpace(s))
				if err != nil {
					continue
				}

				cls = append(cls, cl)
			}
		}
	}
	return cls
}

func newDependencyResolver(opt DependencyResolverOptions) *dependencyResolver {
	return &dependencyResolver{
		DependencyResolverOptions: opt,
		visited:                   map[Change]bool{},
	}
}

func FindDependencies(opt DependencyResolverOptions, roots ...Change) (cls []Change, err error) {
	d := newDependencyResolver(opt)

	for _, cl := range roots {
		err = d.visit(cl, true, true)
		if err != nil {
			return nil, err
		}
	}

	return d.cls, nil
}

func statusIsOpen(status string) bool {
	switch status {
	case "NEW":
		return true
	case "MERGED", "ABANDONED":
		return false
	default:
		logrus.Warnln("unknown CL status", status)
		return false
	}
}

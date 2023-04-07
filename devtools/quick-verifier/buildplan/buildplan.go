// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package buildplan

import (
	"fmt"
	"path"

	"cloud.google.com/go/cloudbuild/apiv1/v2/cloudbuildpb"
)

type Step struct {
	Name       string
	Entrypoint string
	Args       []string
	Dir        string
}

func Command(name, entrypoint string, args ...string) *Step {
	return &Step{
		Name:       name,
		Entrypoint: entrypoint,
		Args:       args,
	}
}

type Sequence struct {
	steps        []*Step
	id           string
	useVolume    bool
	extraVolumes []string
	deps         []*Sequence
}

func Commands(id string, steps ...*Step) *Sequence {
	return &Sequence{
		steps: steps,
		id:    id,
	}
}

func (s *Sequence) WithVolume() *Sequence {
	s.useVolume = true
	return s
}

func (s *Sequence) WithExtraVolume(extraVolume string) *Sequence {
	s.extraVolumes = append(s.extraVolumes, extraVolume)
	return s
}

func (s *Sequence) WithDep(dep *Sequence) *Sequence {
	s.deps = append(s.deps, dep)
	return s
}

func volumeForID(id string) *cloudbuildpb.Volume {
	return &cloudbuildpb.Volume{
		Name: "workspace-" + id,
		Path: "/workspace-" + id,
	}
}

func (s *Sequence) AsCloudBuild() []*cloudbuildpb.BuildStep {
	cbSteps := make([]*cloudbuildpb.BuildStep, len(s.steps))

	volume := volumeForID(s.id)

	for i, step := range s.steps {
		cbSteps[i] = &cloudbuildpb.BuildStep{
			Id:         s.StepID(i),
			Name:       step.Name,
			Entrypoint: step.Entrypoint,
			Args:       step.Args,
		}
		cbStep := cbSteps[i]

		if i == 0 {
			for _, dep := range s.deps {
				cbStep.WaitFor = append(cbStep.WaitFor, dep.LastStepID())
			}
		} else {
			cbStep.WaitFor = append(cbStep.WaitFor, s.StepID(i-1))
		}

		if s.useVolume {
			cbStep.Dir = path.Join(volume.Path, step.Dir)
			cbStep.Volumes = append(cbStep.Volumes, volume)
		} else {
			cbStep.Dir = step.Dir
		}

		for _, extraVolume := range s.extraVolumes {
			cbStep.Volumes = append(cbStep.Volumes, volumeForID(extraVolume))
		}
	}

	return cbSteps
}

func (s *Sequence) StepID(i int) string {
	return fmt.Sprintf("%s:%d", s.id, i)
}

func (s *Sequence) LastStepID() string {
	return s.StepID(len(s.steps) - 1)
}

type Build struct {
	sequences []*Sequence
}

func (b *Build) Add(s *Sequence) *Sequence {
	b.sequences = append(b.sequences, s)
	return s
}

func (b *Build) AsCloudBuild() []*cloudbuildpb.BuildStep {
	var steps []*cloudbuildpb.BuildStep
	for _, seq := range b.sequences {
		steps = append(steps, seq.AsCloudBuild()...)
	}
	return steps
}

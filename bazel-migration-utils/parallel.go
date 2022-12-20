// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import "runtime"

func parallelMap[S any, T any](fn func(S) T, items []S) <-chan T {
	inputs := make(chan S)
	maxprocs := runtime.GOMAXPROCS(0)
	outputs := make(chan T, maxprocs)
	results := make(chan T)

	go func() {
		for _, item := range items {
			inputs <- item
		}
		close(inputs)
	}()

	for i := 0; i < maxprocs; i++ {
		go func() {
			for job := range inputs {
				outputs <- fn(job)
			}
		}()
	}

	go func() {
		for i := 0; i < len(items); i++ {
			results <- <-outputs
		}
		close(outputs)
		close(results)
	}()

	return results
}

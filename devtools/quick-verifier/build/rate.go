// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"time"

	"golang.org/x/time/rate"
)

// Build Create requests per minute = 60.
// But let's use only half of it.
const createRequestsPerMinute = 30

var buildCreateLimiter = rate.NewLimiter(
	rate.Every(time.Minute/createRequestsPerMinute),
	createRequestsPerMinute)

func init() {
	// Drain the limiter at the beginning.
	if ok := buildCreateLimiter.AllowN(time.Now(), createRequestsPerMinute); !ok {
		log.Fatal("limiter is broken")
	}
}

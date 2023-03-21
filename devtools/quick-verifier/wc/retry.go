// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wc

import (
	"context"
	"errors"
	"net/http"

	"github.com/hashicorp/go-retryablehttp"
)

func retryClient() *http.Client {
	c := retryablehttp.NewClient()
	c.HTTPClient.Jar = reloadCookieJar{}
	c.CheckRetry = retryPolicy
	return c.StandardClient()
}

func retryPolicy(ctx context.Context, resp *http.Response, err error) (bool, error) {
	retry, err := retryablehttp.ErrorPropagatedRetryPolicy(ctx, resp, err)
	if retry || err != nil {
		return retry, err
	}

	if resp.StatusCode == http.StatusBadRequest {
		return true, errors.New("Bad request, maybe the cookie expired?")
	}

	return retry, err
}

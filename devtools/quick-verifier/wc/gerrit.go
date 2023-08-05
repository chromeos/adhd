// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wc

import (
	"fmt"
	"log"
	"net/http"

	"github.com/andygrunwald/go-gerrit"
)

type Client struct {
	gerritClients map[GerritHost]*gerrit.Client
}

func (c *Client) Host(h GerritHost) *gerrit.Client {
	gc, ok := c.gerritClients[h]
	if !ok {
		panic(fmt.Errorf("no client for %q", h))
	}
	return gc
}

func NewGerritClient(gerritURL string, jar http.CookieJar) (*gerrit.Client, error) {
	c, err := gerrit.NewClient(
		gerritURL,
		retryClient(jar),
	)
	if err != nil {
		return nil, err
	}

	// set an unused cookie so c.AuthenticationService.HasAuth returns true
	// the actual cookie token is already available with our own ~/.gitcookies
	// based cookie jar
	c.Authentication.SetCookieAuth("unused", "unused")

	account, _, err := c.Accounts.GetAccountDetails("self")
	if err != nil {
		return nil, err
	}
	log.Printf("logged in as %s on %s", account.Email, gerritURL)

	return c, err
}

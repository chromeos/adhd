// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wc

import (
	"fmt"
	"log"

	"github.com/andygrunwald/go-gerrit"
)

type Client struct {
	gerritClients map[GerritHost]*gerrit.Client
}

func NewClient() (*Client, error) {
	c := &Client{
		gerritClients: make(map[GerritHost]*gerrit.Client),
	}

	for _, host := range gerritHosts {
		var err error
		c.gerritClients[host], err = NewGerritClient(host.URL())
		if err != nil {
			return nil, fmt.Errorf("cannot access %s: %v", host.URL(), err)
		}
	}

	return c, nil
}

func (c *Client) Host(h GerritHost) *gerrit.Client {
	gc, ok := c.gerritClients[h]
	if !ok {
		panic(fmt.Errorf("no client for %q", h))
	}
	return gc
}

func NewGerritClient(gerritURL string) (*gerrit.Client, error) {
	r, err := openGitCookies()
	if err != nil {
		return nil, err
	}
	defer r.Close()

	c, err := gerrit.NewClient(
		gerritURL,
		retryClient(),
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

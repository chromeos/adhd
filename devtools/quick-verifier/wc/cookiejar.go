// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wc

import (
	"bufio"
	"io"
	"net/http"
	"net/url"
	"os"
	"os/user"
	"path/filepath"
	"strings"
)

type gitCookieJar []*http.Cookie

var _ http.CookieJar = gitCookieJar{}

func (j gitCookieJar) SetCookies(u *url.URL, cookies []*http.Cookie) {}

func (j gitCookieJar) Cookies(u *url.URL) (cookies []*http.Cookie) {
	for _, c := range j {
		if domainMatch(c.Domain, u.Host) {
			cookies = append(cookies, c)
		}
	}
	return cookies
}

func parseGitCookies(r io.Reader) (http.CookieJar, error) {
	// cURL cookie fields https://curl.se/docs/http-cookies.html
	const (
		domain = iota
		subdomains
		path
		httpsOnly
		expires
		key
		value
	)

	var jar gitCookieJar

	s := bufio.NewScanner(r)
	for s.Scan() {
		line := strings.TrimSpace(s.Text())

		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		fields := strings.Split(line, "\t")

		jar = append(jar, &http.Cookie{
			Domain: fields[domain],
			Name:   fields[key],
			Value:  fields[value],
		})
	}
	if s.Err() != nil {
		return nil, s.Err()
	}

	return jar, nil
}

func openGitCookies() (io.ReadCloser, error) {
	u, err := user.Current()
	if err != nil {
		return nil, err
	}

	path := filepath.Join(u.HomeDir, ".gitcookies")
	return os.Open(path)
}

func domainMatch(cookieDomain, requestHost string) bool {
	if strings.TrimPrefix(cookieDomain, ".") == requestHost {
		return true
	}
	return strings.HasPrefix(cookieDomain, ".") && strings.HasSuffix(requestHost, cookieDomain)
}

type reloadCookieJar struct{}

var _ http.CookieJar = reloadCookieJar{}

func (j reloadCookieJar) SetCookies(u *url.URL, cookies []*http.Cookie) {}

func (j reloadCookieJar) Cookies(u *url.URL) (cookies []*http.Cookie) {
	r, err := openGitCookies()
	if err != nil {
		panic(err)
	}
	defer r.Close()

	staticJar, err := parseGitCookies(r)
	if err != nil {
		panic(err)
	}

	return staticJar.Cookies(u)
}

#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import http.cookiejar
import http.cookies
import json
import logging
import os
import urllib.parse
import urllib.request

import lint_cl


def getcookies(f):
    # https://chromium.googlesource.com/chromium/tools/depot_tools.git/+/c1c45f8a853111bcdb24e2de7a6e8781bf2e9c06/gerrit_util.py#229
    gitcookies = {}
    for line in f:
        try:
            fields = line.strip().split('\t')
            if line.strip().startswith('#') or len(fields) != 7:
                continue
            domain, xpath, key, value = fields[0], fields[2], fields[5], fields[6]
            if xpath == '/' and key == 'o':
                if value.startswith('git-'):
                    gitcookies[domain] = (key, value)
        except (IndexError, ValueError, TypeError) as exc:
            logging.warning(exc)
    return gitcookies


def main():
    p = argparse.ArgumentParser()
    p.add_argument('diags_json')
    with open(p.parse_args().diags_json) as file:
        data = json.load(file)
    change_id = data['change_id']
    revision = data['revision']
    comments = data['comments']

    with open(os.path.expanduser('~/.gitcookies')) as f:
        gitcookies = getcookies(f)
    cookies = http.cookies.SimpleCookie()
    for domain, (key, value) in gitcookies.items():
        if http.cookiejar.domain_match(lint_cl.GERRIT_HOST, domain):
            cookies[key] = value

    for comment in comments:
        # https://gerrit-review.googlesource.com/Documentation/rest-api-changes.html#create-draft
        request = urllib.request.Request(
            urllib.parse.urljoin(
                lint_cl.GERRIT_HOST,
                f'/changes/{change_id}/revisions/{revision}/drafts',
            ),
            data=json.dumps(comment).encode('utf8'),
            headers={
                'Content-Type': 'application/json; charset=UTF-8',
                'Cookie': cookies.output(header='').lstrip(),
            },
            method='PUT',
        )
        print(f'drafting comment {comment["path"]}:{comment["line"]}')
        lint_cl.gerrit_request(request, want_status=201)


if __name__ == '__main__':
    main()

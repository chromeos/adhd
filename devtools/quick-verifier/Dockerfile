# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

FROM alpine:latest as go

RUN apk add go
COPY . /workspace
RUN cd /workspace && CGO_ENABLED=0 go build -o bot ./build

FROM alpine:latest
RUN apk add git python3
RUN git clone --depth=1 https://gerrit.googlesource.com/gcompute-tools
COPY entrypoint /entrypoint
RUN chmod 755 /entrypoint
RUN ln -s ~/.git-credential-cache/cookie ~/.gitcookies
COPY --from=go /workspace/bot /bot
ENTRYPOINT /entrypoint

# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

FROM alpine:latest as go

RUN apk add go
COPY . /workspace
RUN cd /workspace && CGO_ENABLED=0 go build -o bot ./cmd/quick-verifier

FROM alpine:latest
COPY entrypoint /entrypoint
RUN chmod 755 /entrypoint
COPY --from=go /workspace/bot /bot
ENTRYPOINT /entrypoint

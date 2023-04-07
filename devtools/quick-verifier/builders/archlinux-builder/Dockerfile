# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

FROM archlinux as base
RUN pacman -Sy archlinux-keyring --noconfirm --cachedir=/paccache \
  && pacman -Su --needed --noconfirm --cachedir=/paccache \
  base-devel \
  bazel \
  git \
  vim \
  alsa-lib \
  sbc \
  speexdsp \
  clang \
  ladspa \
  gtest \
  rust \
  python \
  lld \
  llvm \
  protobuf \
  rsync \
  && rm -rf /paccache

FROM archlinux as kythe
RUN \
  mkdir -p /opt/kythe \
  && curl -L https://github.com/kythe/kythe/releases/download/v0.0.60/kythe-v0.0.60.tar.gz | tar --strip-components=1 --no-same-owner --directory=/opt/kythe -xzf -

FROM base as bazel-cache
RUN git clone https://chromium.googlesource.com/chromiumos/third_party/adhd
RUN cd adhd && bazel build --nobuild --config=local-clang //...

FROM base as builder
COPY build_kzip.bash compdb_fixup.py /
COPY bazelrc /etc/bazel.bazelrc
COPY --from=kythe /opt/kythe /opt/kythe
RUN echo 'export PATH=/root/.cargo/bin:$PATH' >> /root/.bashrc
COPY --from=bazel-cache /root/.cache/bazel /root/.cache/bazel

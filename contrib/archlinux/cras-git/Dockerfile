# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Dockerfile to test the build.

FROM archlinux:latest
RUN pacman -Sy archlinux-keyring --noconfirm \
  && pacman -Syu sudo base-devel --needed --noconfirm
RUN useradd -m -d /work builduser
RUN echo 'builduser ALL=(ALL) NOPASSWD:/usr/bin/pacman' >> /etc/sudoers

RUN cat /usr/share/makepkg/util/util.sh
COPY --chown=builduser PKGBUILD /work/PKGBUILD
RUN cd /work && runuser -u builduser -- makepkg --syncdeps --noconfirm --install

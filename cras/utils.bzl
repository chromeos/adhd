# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def define_feature(condition, define):
    """Defines `define`=1 if condition is true, otherwise 0"""
    return select({
        condition: ["{}=1".format(define)],
        "//conditions:default": ["{}=0".format(define)],
    })

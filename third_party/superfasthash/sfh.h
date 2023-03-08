/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * An incremental version of the SuperFastHash hash function from
 * http://www.azillionmonkeys.com/qed/hash.html
 * The code did not come with its own header file, so declaring the function
 * here.
 */

#ifndef THIRD_PARTY_SUPERFASTHASH_SFH_H_
#define THIRD_PARTY_SUPERFASTHASH_SFH_H_

#include <stdint.h>

uint32_t SuperFastHash(const char* data, int len, uint32_t hash);

#endif  // THIRD_PARTY_SUPERFASTHASH_SFH_H_

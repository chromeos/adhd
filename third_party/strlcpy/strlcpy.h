/*
 * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef THIRD_PARTY_STRLCPY_STRLCPY_H_
#define THIRD_PARTY_STRLCPY_STRLCPY_H_

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GLIBC__) || defined(_WIN32)
// Declaration of strlcpy() for platforms that don't already have it.
size_t strlcpy(char* dst, const char* src, size_t size);
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif

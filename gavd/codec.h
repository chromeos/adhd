/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_CODEC_H_)
#define _CODEC_H_

/* codec_desc_t: A codec descriptor.
 *
 *   This structure is used to map a known hardware codec to a known
 *   board.
 *
 *   codec            : A compile-time name for the hardware codec.
 *                      May not be NULL.
 *
 *   board            : A compile-time name for the board.
 *                      May not be NULL.
 *
 *   headphone_insert : Set of 'amixer' commands used to enable
 *                      external headphones.
 *                      NULL or a NULL-terminated array of strings.
 *
 *   headphone_remove : Set of 'amixer' commands used to enable
 *                      internal speakers.
 *                      NULL or a NULL-terminated array of strings.
 *
 *   microphone_insert: Set of 'amixer' commands used to enable
 *                      external microphone.
 *                      NULL or a NULL-terminated array of strings.
 *
 *   microphone_remove: Set of 'amixer' commands used to enable
 *                      internal microphone.
 *                      NULL or a NULL-terminated array of strings.
 */

typedef struct codec_desc_t {
    const char  *codec;
    const char  *board;
    const char **headphone_insert;  /* NULL or NULL terminated array */
    const char **headphone_remove;  /* NULL or NULL terminated array */
    const char **microphone_insert; /* NULL or NULL terminated array */
    const char **microphone_remove; /* NULL or NULL terminated array */
} codec_desc_t;

/* For the following hardware management functions, success means the
 * following:
 *
 *    o No codec was matched at start up (no known commands to execute)
 *    o The codec has no commands for the action
 *    o All the commands associated with the action completed successfully.
 *
 * Failure means:
 *
 *    o One of the 'amixer' commands associated with the action failed.
 */

unsigned codec_headphone_insert(void);       /* 0: failure
                                              * 1: success
                                              */
unsigned codec_headphone_remove(void);       /* 0: failure
                                              * 1: success
                                              */
unsigned codec_microphone_insert(void);      /* 0: failure
                                              * 1: success
                                              */
unsigned codec_microphone_remove(void);      /* 0: failure
                                              * 1: success
                                              */
#endif

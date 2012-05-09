/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <string.h>
#include <stdio.h>

#include "adhd_alsa_defs.h"
#include "board.h"
#include "codec.h"
#include "initialization.h"
#include "linkerset.h"
#include "utils.h"
#include "verbose.h"

LINKERSET_DECLARE(codec_desc);

static const codec_desc_t *codec;

static const codec_desc_t *find_codec_desc_by_board(const char *board)
{
    LINKERSET_ITERATE(codec_desc, ls, {
            const codec_desc_t *c = (const codec_desc_t *)ls;

            if (strcmp(c->board, board) == 0) {
                return c;
            }
        });
    return NULL;
}

static unsigned execute_commands(const char **c)
{
    static const char amixer[] = ADHD_AMIXER_COMMAND;
    const unsigned amixer_len  = (unsigned)(sizeof(amixer) / sizeof(amixer[0]));

    // invariant: c is a NULL-terminated array
    // invariant: result = 0 -> failure
    //            result = 1 -> success
    assert(c != NULL);
    while (c[0] != NULL) {
        const unsigned cmd_len = (unsigned)strlen(c[0]);
        const unsigned len     = (amixer_len +
                                  1 /* space */ +
                                  cmd_len +
                                  1 /* '\0' */);
        char *cmd = malloc(len);

        if (cmd != NULL) {
            unsigned result;

            snprintf(cmd, len, "%s %s", amixer, c[0]);
            result = utils_execute_command(cmd);

            if (result == 0) {
                verbose_log(0, LOG_WARNING, "%s: '%s': failure",
                            __FUNCTION__, cmd);
                return 0;
            }
            free(cmd);
        }
        ++c;
    }
    return 1;
}

unsigned codec_headphone_insert(void)
{
    return (codec                   == NULL ||
            codec->headphone_insert == NULL ||
            execute_commands(codec->headphone_insert));
}

unsigned codec_headphone_remove(void)
{
    return (codec                   == NULL ||
            codec->headphone_remove == NULL ||
            execute_commands(codec->headphone_remove));
}

unsigned codec_microphone_insert(void)
{
    return (codec                    == NULL ||
            codec->microphone_insert == NULL ||
            execute_commands(codec->microphone_insert));
}

unsigned codec_microphone_remove(void)
{
    return (codec                    == NULL ||
            codec->microphone_remove == NULL ||
            execute_commands(codec->microphone_remove));
}

static void initialize(void)
{
    codec = find_codec_desc_by_board(ADHD_TARGET_MACHINE);
    if (codec != NULL) {
        verbose_log(5, LOG_WARNING, "%s: codec '%s' for board '%s'",
                    __FUNCTION__, codec->codec, codec->board);
    } else {
        verbose_log(5, LOG_WARNING, "%s: Board '%s' not found.", __FUNCTION__,
                    ADHD_TARGET_MACHINE);
    }
}

static void finalize(void)
{
}

INITIALIZER("Codec Management", initialize, finalize);

/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This module implements communication to the Chromium Audio Server
 * (cras) daemon via a control socket created by cras.
 */
#include <assert.h>
#include <stdlib.h>
#include <cras_messages.h>
#include <cras_client.h>

#include "initialization.h"
#include "verbose.h"
#include "cras_connection.h"

static struct cras_client *cras_client;

void notify_cras(unsigned action,
                 unsigned card_number,
                 unsigned device_number,
                 unsigned active,
                 unsigned internal,
                 unsigned primary)
{
    if (cras_client != NULL) {
        int res = cras_client_notify_device(cras_client,
                                            action, card_number,
                                            device_number, active,
                                            internal, primary);
        verbose_log(0, LOG_INFO, "%s: cras client notified: %d",
                    __FUNCTION__, res);
        assert(res == 0 || res == -EPIPE);
    }
}

static void initialize_cras(void)
{
    if (cras_client_create(&cras_client) < 0) {
        cras_client = NULL;
        verbose_log(0, LOG_ERR, "%s: could not create connection for 'cras'",
                    __FUNCTION__);
    } else {
        int err;

        err = cras_client_connect(cras_client);
        if (err) {
            cras_client = NULL;
            verbose_log(0, LOG_ERR, "%s: could not open cras socket: %d",
                        __FUNCTION__, err);
        } else {
            verbose_log(0, LOG_INFO, "%s: cras socket opened",
                        __FUNCTION__);
        }
    }
}

static void finalize_cras(void)
{
    if (cras_client != NULL) {
        cras_client_destroy(cras_client);
        verbose_log(5, LOG_INFO, "%s: cras connection destroyed.",
                    __FUNCTION__);
    } else {
        verbose_log(5, LOG_INFO, "%s: no cras connection to destroy.",
                    __FUNCTION__);
    }
}

INITIALIZER("cras socket communication", initialize_cras, finalize_cras);


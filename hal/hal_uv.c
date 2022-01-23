/*
 * Copyright (C) 2020 Simon Piriou. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "adb.h"
#include "hal_uv_priv.h"
#include <uv.h>

static adb_context_uv_t g_adbd_context;

/****************************************************************************
 * HAL Public Functions
 ****************************************************************************/

adb_context_t* adb_hal_create_context() {
    adb_context_uv_t *adbd = &g_adbd_context;

#ifdef __NUTTX__
    uv_library_init(&adbd->uv_context);
    adbd->loop = uv_default_loop(&adbd->uv_context);
#else
    adbd->loop = uv_default_loop();
#endif
    adbd->context.clients = NULL;

#ifdef CONFIG_ADBD_TCP_SERVER
    if (adb_uv_tcp_setup(adbd)) {
        goto exit_fail;
    }
#endif

#ifdef CONFIG_ADBD_USB_SERVER
    if (adb_uv_usb_setup(adbd, "/dev/adb0")) {
        goto exit_fail;
    }
#endif

    return &adbd->context;

exit_fail:
#ifdef __NUTTX__
    uv_library_shutdown(&adbd->uv_context);
#endif
    return NULL;
}

void adb_hal_destroy_context(adb_context_t *context) {
    UNUSED(context);
}

adb_client_t *adb_hal_create_client(size_t size) {
    return (adb_client_t*)malloc(size);
}

void adb_hal_destroy_client(adb_client_t *client) {
    free(client);
}

int adb_hal_run(adb_context_t *context) {
    int ret;
    adb_context_uv_t *adbd =
        container_of(context, adb_context_uv_t, context);

    ret = uv_run(adbd->loop, UV_RUN_DEFAULT);
    adb_log("uv_loop exit %d\n", ret);

    return 0;
}

#ifdef CONFIG_ADBD_AUTHENTICATION
int adb_hal_random(void *buf, size_t len) {
    return uv_random(NULL, NULL, buf, len, 0, NULL);
}
#endif

/****************************************************************************
 * Hal internal functions
 ****************************************************************************/

adb_client_uv_t* adb_uv_create_client(size_t size) {
    adb_client_uv_t *client;
    client = (adb_client_uv_t*)adb_create_client(size);
    if (client == NULL) {
        return NULL;
    }

    client->cur_packet = NULL;
    client->frame_count = 0;
    return client;
}

void adb_uv_close_client(adb_client_uv_t *client) {

    if (client->cur_packet) {
        adb_hal_apacket_release(&client->client, &client->cur_packet->p);
        client->cur_packet = NULL;
    }

    adb_destroy_client(&client->client);
}

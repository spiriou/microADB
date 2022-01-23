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

#include "adb.h"
#include "hal_uv_priv.h"
#include <uv.h>

/****************************************************************************
 * Private types
 ****************************************************************************/

typedef struct adb_client_usb_s {
    adb_client_uv_t uc;
    /* FIXME libuv handle must be right after adb_client_uv_t */
    uv_pipe_t pipe;
} adb_client_usb_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void usb_uv_allocate_frame(uv_handle_t* handle,
                       size_t suggested_size, uv_buf_t* buf) {
    UNUSED(suggested_size);

    adb_client_usb_t *client = container_of(handle, adb_client_usb_t, pipe);
    adb_uv_allocate_frame(&client->uc, buf);
}

static void usb_uv_on_data_available(uv_stream_t* handle,
        ssize_t nread, const uv_buf_t* buf) {
    adb_client_usb_t *client = container_of(handle, adb_client_usb_t, pipe);

    adb_uv_on_data_available(&client->uc, handle, nread, buf);
}

static int usb_uv_write(adb_client_t *c, apacket *p) {
    int ret;
    uv_buf_t buf[2];
    int buf_cnt;
    apacket_uv_t *up = container_of(p, apacket_uv_t, p);
    adb_client_usb_t *client = container_of(c, adb_client_usb_t, uc.client);

    buf[0].base = (char*)&p->msg;
    buf[0].len  = sizeof(p->msg);

    if (p->msg.data_length > 0) {
        buf[1].base = (char*)p->data;
        buf[1].len  = p->msg.data_length;
        buf_cnt = 2;
    }
    else {
        buf_cnt = 1;
    }

    /* Packet is now tracked by libuv */
    up->wr.data = &client->uc;

    ret =uv_write(&up->wr, (uv_stream_t*)&client->pipe, buf, buf_cnt,
        adb_uv_after_write);
    if (ret) {
        adb_log("uv_write failed %d %d\n", ret, errno);
        /* Caller will destroy client */
        return -1;
    }

    return 0;
}

static void usb_uv_kick(adb_client_t *c) {
    adb_client_usb_t *client = container_of(c, adb_client_usb_t, uc.client);

    if (!uv_is_active((uv_handle_t*)&client->pipe)) {
        /* Restart read events */
        int ret = uv_read_start((uv_stream_t*)&client->pipe,
            usb_uv_allocate_frame,
            usb_uv_on_data_available);
        /* TODO check return code */
        assert(ret == 0);
    }

    adb_client_kick_services(c);
}

static void usb_uv_on_close(uv_handle_t* handle) {
    adb_client_usb_t *client = container_of(handle, adb_client_usb_t, pipe);

    adb_uv_close_client(&client->uc);
}

static void usb_uv_close(adb_client_t *c) {
    adb_client_usb_t *client = (adb_client_usb_t*)c;

    /* Close pipe and cancel all pending write requests if any */
    uv_close((uv_handle_t*)&client->pipe, usb_uv_on_close);
}

static const adb_client_ops_t adb_usb_uv_ops = {
    .write = usb_uv_write,
    .kick  = usb_uv_kick,
    .close = usb_uv_close
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int adb_uv_usb_setup(adb_context_uv_t *adbd, const char *path) {
    int ret;
    int fd;
    adb_client_usb_t *client;

    client = (adb_client_usb_t*)adb_uv_create_client(sizeof(*client));
    if (client == NULL) {
        adb_log("failed to allocate usb client\n");
        return -ENOMEM;
    }

    /* Setup adb_client */

    client->uc.client.ops = &adb_usb_uv_ops;

    ret = uv_pipe_init(adbd->loop, &client->pipe, 0);
    client->pipe.data = adbd;
    if (ret) {
        adb_log("usb init error %d %d\n", ret, errno);
        return ret;
    }

    fd = open(path, O_RDWR | O_CLOEXEC);

    if (fd < 0) {
        adb_log("failed to open usb device %d %d\n", fd, errno);
        return fd;
    }

    ret = uv_pipe_open(&client->pipe, fd);
    if (ret) {
        adb_log("usb pipe open error %d %d\n", ret, errno);
        close(fd);
        return ret;
    }

    ret = uv_read_start((uv_stream_t*)&client->pipe,
        usb_uv_allocate_frame,
        usb_uv_on_data_available);
    /* TODO check return code */
    assert(ret == 0);

    /* Insert client in context */
    adb_register_client(&client->uc.client, &adbd->context);

    return 0;
}

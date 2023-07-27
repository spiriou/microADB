/*
 * Copyright (C) 2022 Simon Piriou. All rights reserved.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "adb.h"
#include "tcp_service.h"
#include "hal_uv_priv.h"

static void tcp_stream_allocate_frame(uv_handle_t* handle,
                       size_t suggested_size,
                       uv_buf_t* buf) {
    UNUSED(suggested_size);

    apacket_uv_t *up;
    adb_client_uv_t *client = (adb_client_uv_t*)handle->data;

    up = adb_uv_packet_allocate(client, 0);
    if (up == NULL) {
        /* Out of apacket. Try again later. */
        buf->len = 0;
        return;
    }

    buf->base = (char*)up->p.data;
    buf->len = CONFIG_ADBD_PAYLOAD_SIZE;
}

static void tcp_stream_on_data_available(uv_stream_t* handle,
        ssize_t nread, const uv_buf_t* buf) {
    UNUSED(handle);
    UNUSED(nread);
    UNUSED(buf);

    apacket_uv_t *ap = container_of(buf->base, apacket_uv_t, p.data);
    adb_tcp_socket_t *socket = container_of(handle, adb_tcp_socket_t, handle);
    adb_client_t *client = (adb_client_t*)socket->handle.data;

    if (nread == UV_ENOBUFS) {
        uv_read_stop((uv_stream_t*)&socket->handle);
        return;
    }

    if (nread == 0) {
        adb_hal_apacket_release(client, &ap->p);
        return;
    }

    if (nread > 0) {
        uv_read_stop((uv_stream_t*)&socket->handle);
        ap->p.msg.data_length = nread;
    }
    else {
        /* Notify service an error occured. */
        ap->p.msg.data_length = 0;
    }

    socket->on_data_cb(socket, &ap->p);
}

static void socket_close_cb(uv_handle_t* handle) {
    adb_tcp_socket_t *socket = container_of(handle, adb_tcp_socket_t, handle);
    socket->close_cb(socket);
}

void adb_hal_socket_close(adb_tcp_socket_t *socket, void (*close_cb)(adb_tcp_socket_t*)) {
    socket->close_cb = close_cb;
    uv_close((uv_handle_t*)&socket->handle, socket_close_cb);
}

int adb_hal_socket_start(adb_tcp_socket_t *socket,
    void (*on_data_cb)(adb_tcp_socket_t*, apacket*)) {

    socket->on_data_cb = on_data_cb;

    if (!uv_is_active((uv_handle_t*)&socket->handle)) {
        assert(0 == uv_read_start((uv_stream_t*)&socket->handle,
                                tcp_stream_allocate_frame,
                                tcp_stream_on_data_available));
    }

    return 0;
}

int adb_hal_socket_stop(adb_tcp_socket_t *socket) {
    return uv_read_stop((uv_stream_t*)&socket->handle);
}

static void fwd_tcp_after_write(uv_write_t* req, int status) {
    apacket_uv_t *up = container_of(req, apacket_uv_t, wr);
    adb_tcp_socket_t *socket = (adb_tcp_socket_t*)req->data;
    adb_client_t *client = (adb_client_t*)socket->handle.data;

    socket->on_write_cb(client, socket, &up->p, status < 0);
}

int adb_hal_socket_write(adb_tcp_socket_t *socket, apacket *p,
    void (*cb)(adb_client_t*, adb_tcp_socket_t*, apacket*, bool)) {
    int ret;
    uv_buf_t buf;
    apacket_uv_t *uv_p = container_of(p, apacket_uv_t, p);

    buf = uv_buf_init((char*)&p->data,
        p->msg.data_length);

    /* Packet is now tracked by libuv */
    uv_p->wr.data = socket;
    socket->on_write_cb = cb;

    ret = uv_write(&uv_p->wr, (uv_stream_t*)&socket->handle, &buf, 1, fwd_tcp_after_write);
    if (ret) {
        adb_err("uv_write failed (len=%d, ret=%d, errno=%d)\n", buf.len, ret, errno);
        return -1;
    }

    return 0;
}

static void connect_cb(uv_connect_t* req, int status) {
    adb_tcp_conn_t *conn = container_of(req, adb_tcp_conn_t, connect_req);
    adb_tcp_socket_t *socket = (adb_tcp_socket_t *)req->data;

    conn->on_connect_cb(socket, status);
}

int adb_hal_socket_connect(adb_client_t *client, adb_tcp_socket_t *socket,
                           int port, adb_tcp_conn_t *conn,
                           void (*on_connect_cb)(adb_tcp_socket_t*, int)) {
    struct sockaddr_in addr;
    uv_handle_t *handle = (uv_handle_t*)(((adb_client_uv_t*)client)+1);

    assert(0 == uv_ip4_addr("127.0.0.1", port, &addr));
    assert(0 == uv_tcp_init(handle->loop, &socket->handle));

    socket->handle.data = client;
    conn->on_connect_cb = on_connect_cb;
    conn->connect_req.data = socket;

    return uv_tcp_connect(&conn->connect_req,
                          &socket->handle,
                          (const struct sockaddr*) &addr,
                          connect_cb);
}

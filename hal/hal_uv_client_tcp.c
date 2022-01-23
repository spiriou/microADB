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

#include <arpa/inet.h>

#include "adb.h"
#include "hal_uv_priv.h"
#include <uv.h>

/****************************************************************************
 * Private types
 ****************************************************************************/

typedef struct adb_client_tcp_s {
    adb_client_uv_t uc;
    /* libuv handle must be right after adb_client_uv_t */
    uv_tcp_t socket;
} adb_client_tcp_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void tcp_uv_allocate_frame(uv_handle_t* handle,
                       size_t suggested_size, uv_buf_t* buf) {
    UNUSED(suggested_size);

    adb_client_tcp_t *client = container_of(handle, adb_client_tcp_t, socket);
    adb_uv_allocate_frame(&client->uc, buf);
}

static void tcp_uv_on_data_available(uv_stream_t* handle,
        ssize_t nread, const uv_buf_t* buf) {
    adb_client_tcp_t *client = container_of(handle, adb_client_tcp_t, socket);

    adb_uv_on_data_available(&client->uc, handle, nread, buf);
}

static int tcp_uv_write(adb_client_t *c, apacket *p) {
    int ret;
    uv_buf_t buf;
    apacket_uv_t *up = container_of(p, apacket_uv_t, p);
    adb_client_tcp_t *client = container_of(c, adb_client_tcp_t, uc.client);

    buf = uv_buf_init((char*)&p->msg,
        sizeof(p->msg) + p->msg.data_length);

    /* Packet is now tracked by libuv */
    up->wr.data = &client->uc;

    ret = uv_write(&up->wr, (uv_stream_t*)&client->socket, &buf, 1,
        adb_uv_after_write);
    if (ret) {
        adb_log("uv_write failed %d %d\n", ret, errno);
        /* Caller will destroy client */
        return -1;
    }

    return 0;
}

static void tcp_uv_kick(adb_client_t *c) {
    adb_client_tcp_t *client = container_of(c, adb_client_tcp_t, uc.client);

    if (!uv_is_active((uv_handle_t*)&client->socket)) {
        /* Restart read events */
        int ret = uv_read_start((uv_stream_t*)&client->socket,
            tcp_uv_allocate_frame,
            tcp_uv_on_data_available);
        /* TODO check return code */
        assert(ret == 0);
    }

    adb_client_kick_services(c);
}

static void tcp_uv_on_close(uv_handle_t* handle) {
    adb_client_tcp_t *client = container_of(handle, adb_client_tcp_t, socket);

    adb_uv_close_client(&client->uc);
}

static void tcp_uv_close(adb_client_t *c) {
    adb_client_tcp_t *client = (adb_client_tcp_t*)c;

    /* Close socket and cancel all pending write requests if any */
    uv_close((uv_handle_t*)&client->socket, tcp_uv_on_close);
}

static const adb_client_ops_t adb_tcp_uv_ops = {
    .write = tcp_uv_write,
    .kick  = tcp_uv_kick,
    .close = tcp_uv_close
};

static void tcp_on_connection(uv_stream_t* server, int status) {
    int ret;
    adb_client_tcp_t *client;
    adb_context_uv_t *adbd = (adb_context_uv_t*)server->data;

    if (status < 0) {
        adb_log("connect failed %d\n", status);
        return;
    }

    client = (adb_client_tcp_t*)adb_uv_create_client(sizeof(*client));
    if (client == NULL) {
        adb_log("failed to allocate stream\n");
        return;
    }

    /* Setup adb_client */
    client->uc.client.ops = &adb_tcp_uv_ops;

    ret = uv_tcp_init(adbd->loop, &client->socket);
    /* TODO check return code */
    assert(ret == 0);

    client->socket.data = server;


    ret = uv_accept(server, (uv_stream_t*)&client->socket);
    /* TODO check return code */
    assert(ret == 0);

    ret = uv_read_start((uv_stream_t*)&client->socket,
        tcp_uv_allocate_frame,
        tcp_uv_on_data_available);
    /* TODO check return code */
    assert(ret == 0);

    /* Insert client in context */
    adb_register_client(&client->uc.client, &adbd->context);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int adb_uv_tcp_setup(adb_context_uv_t *adbd) {
    int ret;
    struct sockaddr_in addr;

    ret = uv_tcp_init(adbd->loop, &adbd->tcp_server);
    adbd->tcp_server.data = adbd;
    if (ret) {
        adb_log("tcp server init error %d %d\n", ret, errno);
        return ret;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(CONFIG_ADBD_TCP_SERVER_PORT);

    ret = uv_tcp_bind(&adbd->tcp_server, (const struct sockaddr*)&addr, 0);
    if (ret) {
        adb_log("tcp server bind error %d %d\n", ret, errno);
        return ret;
    }

    ret = uv_listen((uv_stream_t*)&adbd->tcp_server,
        SOMAXCONN, tcp_on_connection);
    if (ret) {
        adb_log("tcp server listen error %d %d\n", ret, errno);
        return ret;
    }
    return 0;
}

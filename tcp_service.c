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

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include "adb.h"
#include "tcp_service.h"
#include "hal/hal_uv_priv.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
#define REVERSE_LIST_FMT "host tcp:%d tcp:%d\n"

enum stream_state {
    F_ERROR_CLOSE = 0,
    F_NOT_CONNECTED,
    F_NOTIFY_CLIENT, // forward only (we must send OKAY packet)
    F_WAIT_OPEN_ACK, // reverse only (xe must send OPEN packet)
    F_CONNECTED, // Connected and ready to receive packets
    F_WAIT_ACK, // Connected but it cannot receive packets, waiting for ack
};

/* Common structure for reverse and forward streams */
typedef struct adb_stream_service_s {
    adb_service_t service;
    adb_client_t *client;
    adb_tcp_socket_t socket;
    enum stream_state state;
} adb_stream_service_t;

/* adb forward service */

typedef struct atcp_fstream_service_s {
    adb_stream_service_t stream_service;
    adb_tcp_conn_t conn;
} atcp_fstream_service_t;

/* adb reverse services */

struct atcp_reverse_server_s;

typedef struct atcp_rstream_service_s {
    adb_stream_service_t stream_service;
    struct atcp_reverse_server_s *parent;
} atcp_rstream_service_t;

typedef struct atcp_reverse_server_s {
    adb_reverse_server_t server;
    adb_tcp_server_t socket;
} atcp_reverse_server_t;







static void tcp_error_printf(apacket *p, const char *fmt, ...)
{
    int len;
    va_list ap;
    char c;
    va_start(ap, fmt);

    len = vsnprintf((char*)p->data+8, CONFIG_ADBD_PAYLOAD_SIZE-1-8, fmt, ap);
    if (len >= CONFIG_ADBD_PAYLOAD_SIZE-1-8) {
        assert(0);
    }

    c = p->data[8];
    snprintf((char*)p->data, 8+1, "FAIL%04x", len);
    p->data[8] = c;
    p->write_len = len + 8;
}













static int atcp_stream_on_ack(adb_service_t *service, apacket *p);
static int atcp_stream_on_write(adb_service_t *service, apacket *p);
static void atcp_stream_on_kick(adb_service_t *service);
static void atcp_stream_on_close(struct adb_service_s *service);

static const adb_service_ops_t atcp_stream_ops = {
    .on_write_frame = atcp_stream_on_write,
    .on_ack_frame   = atcp_stream_on_ack,
    .on_kick        = atcp_stream_on_kick,
    .on_close       = atcp_stream_on_close
};

static void atcp_server_release(adb_tcp_server_t *server) {
    atcp_reverse_server_t *svc =
        container_of(server, atcp_reverse_server_t, socket);
    adb_log("releasing reverse %d -> %d\n",
            svc->server.local_port, svc->server.remote_port);
    free(svc);
}

void adb_hal_destroy_reverse_server(struct adb_reverse_server_s *server) {
    atcp_reverse_server_t *svc = container_of(server, atcp_reverse_server_t, server);
    adb_hal_server_close(&svc->socket, atcp_server_release);
}





static void tcp_stream_on_data_cb(adb_tcp_socket_t *socket, apacket *p) {
    adb_stream_service_t *svc;

    svc = container_of(socket, adb_stream_service_t, socket);

    if (svc->state != F_CONNECTED) {
        adb_log("Invalid service state %d\n", svc->state);
        goto exit_close_service;
    }

    if (p->msg.data_length <= 0) {
        adb_log("tcp stream error %d\n", p->msg.data_length);
        goto exit_close_service;
    }

    p->write_len = p->msg.data_length;
    adb_hal_socket_stop(socket);
    svc->state = F_WAIT_ACK;

    /* Write payload from service */
    p->msg.arg0 = svc->service.id;
    p->msg.arg1 = svc->service.peer_id;
    adb_send_data_frame(svc->client, p);
    return;

exit_close_service:
    adb_service_close(svc->client, &svc->service, p);
}

static int atcp_stream_on_ack(adb_service_t *service, apacket *p) {
    UNUSED(p);
    adb_stream_service_t *svc =
        container_of(service, adb_stream_service_t, service);

    if (svc->state == F_WAIT_ACK) {
        svc->state = F_CONNECTED;
        adb_hal_socket_start(&svc->socket, tcp_stream_on_data_cb);
    }
    return 0;
}

static void try_open(adb_stream_service_t *svc)
{
    int ret;
    apacket *p;
    adb_log("entry\n");
    atcp_rstream_service_t *rsvc = container_of(svc, atcp_rstream_service_t, stream_service);

    p = adb_hal_apacket_allocate(svc->client);
    if (p == NULL) {
        /* kick callback will be called when a packet is free */
        return;
    }

    svc->state = F_CONNECTED;
    adb_hal_socket_start(&svc->socket, tcp_stream_on_data_cb);

    ret = sprintf((char*)p->data, "tcp:%d", rsvc->parent->server.remote_port);
    adb_send_open_frame(svc->client, p,
                        svc->service.id, 0, ret+1);
}

static void try_close(adb_stream_service_t *svc)
{
    apacket *p;

    p = adb_hal_apacket_allocate(svc->client);
    if (p == NULL) {
      return;
    }

    adb_service_close(svc->client, &svc->service, p);
}

static void try_connect(adb_stream_service_t *svc)
{
    apacket *p;

    p = adb_hal_apacket_allocate(svc->client);
    if (p == NULL) {
      return;
    }

    svc->state = F_CONNECTED;
    adb_hal_socket_start(&svc->socket, tcp_stream_on_data_cb);

    /* Service successfully connected */
    adb_send_okay_frame(svc->client, p, svc->service.id, svc->service.peer_id);
}

static void atcp_stream_on_kick(adb_service_t *service) {
    adb_stream_service_t *svc =
        container_of(service, adb_stream_service_t, service);

    switch (svc->state) {
        case F_NOT_CONNECTED:
        case F_WAIT_ACK:
            break;
        case F_WAIT_OPEN_ACK:
            /* New connection to reverse server. OPEN needs to be sent */
            try_open(svc);
            break;
        case F_CONNECTED:
            /* Resume read after failed allocation */
            adb_hal_socket_start(&svc->socket, tcp_stream_on_data_cb);
            break;
        case F_NOTIFY_CLIENT:
            try_connect(svc);
            break;
        case F_ERROR_CLOSE:
        default:
            try_close(svc);
            break;
    }
}

static void stream_send_data_frame_cb(adb_client_t *client, adb_tcp_socket_t *socket,
                                      apacket *p, bool fail) {
    adb_stream_service_t *svc =
        container_of(socket, adb_stream_service_t, socket);

    if (fail)
        adb_service_close(client, &svc->service, p);
    else
        adb_send_okay_frame(client, p, svc->service.id, svc->service.peer_id);
}

static int atcp_stream_on_write(adb_service_t *service, apacket *p) {
    int ret;
    adb_stream_service_t *svc =
        container_of(service, adb_stream_service_t, service);

    if (svc->state < F_CONNECTED)
        return -1;

    ret = adb_hal_socket_write(&svc->socket, p, stream_send_data_frame_cb);

    if (ret < 0) {
        adb_log("adb_hal_socket_write failed (ret=%d, errno=%d)\n", ret, errno);
        return -1;
    }

    /* Notify client packet requires async processing */
    return 1;
}

static void atcp_stream_release(adb_tcp_socket_t *socket) {
    adb_stream_service_t *svc =
        container_of(socket, adb_stream_service_t, socket);
    free(svc);
}

static void atcp_stream_on_close(struct adb_service_s *service) {
    adb_stream_service_t *svc =
        container_of(service, adb_stream_service_t, service);

    if (svc->state > F_NOT_CONNECTED) {
        adb_hal_socket_close(&svc->socket, atcp_stream_release);
    }
    else {
        atcp_stream_release(&svc->socket);
    }
}

static void tcp_stream_on_connect_cb(adb_tcp_socket_t *socket, int status) {
    adb_stream_service_t *svc =
        container_of(socket, adb_stream_service_t, socket);

    if (status || svc->state != F_NOT_CONNECTED) {
        adb_log("connect failed (%d)\n", status);
        svc->state = F_ERROR_CLOSE;
        /* According to ADB protocol, the local-id MUST be zero
         * to indicate with this CLOSE a failed OPEN.
         */
        svc->service.id = 0;
        try_close(svc);
        return;
    }

    /* ADB client needs to be notified that connection is successful */
    svc->state = F_NOTIFY_CLIENT;
    try_connect(svc);
}

/* TCP forward socket service creation */

adb_service_t* tcp_forward_service(adb_client_t *client, const char *params, apacket *p)
{
    int port;
    int ret;
    atcp_fstream_service_t *fsvc;

    port = atoi(&params[4]);
    if (port == 0) {
        return NULL;
    }

    fsvc = (atcp_fstream_service_t*)malloc(sizeof(atcp_fstream_service_t));
    if (fsvc == NULL) {
        return NULL;
    }

    fsvc->stream_service.client = client;
    fsvc->stream_service.state = F_NOT_CONNECTED;

    adb_log("connect to port %d\n", port);
    ret = adb_hal_socket_connect(client,
        &fsvc->stream_service.socket,
        port,
        &fsvc->conn,
        tcp_stream_on_connect_cb);

    if (ret) {
        adb_log("Failed to connect to port %d (ret=%d, errno=%d)\n",
                port, ret, errno);
        free(fsvc);
        return NULL;
    }

    fsvc->stream_service.service.ops = &atcp_stream_ops;

    /* Do not send OKAY now but wait for connect callback. */
    p->write_len = APACKET_SERVICE_INIT_ASYNC;
    return &fsvc->stream_service.service;
}

/* Reverse server creation */

static void tcp_reverse_on_connect_cb(adb_tcp_server_t *socket,
    adb_tcp_socket_t *stream_socket) {
    atcp_rstream_service_t *rsvc = container_of(stream_socket, atcp_rstream_service_t, stream_service.socket);
    atcp_reverse_server_t *svc = container_of(socket, atcp_reverse_server_t, socket);

    adb_log("entry %d %d\n", svc->server.local_port, svc->server.remote_port);

    // Register service properly now
    rsvc->stream_service.service.ops = &atcp_stream_ops;
    rsvc->parent = svc;
    adb_register_service(&rsvc->stream_service.service, rsvc->stream_service.client);

    rsvc->stream_service.state = F_WAIT_OPEN_ACK;
    try_open(&rsvc->stream_service);
}

static adb_service_t* tcp_reverse_create_service(adb_client_t *client, const char *params, apacket *p)
{
    int ret;
    int local_port, remote_port;
    atcp_reverse_server_t *server;

    params += 12; // Skip "forward:tcp:"

    /* This is remote port for adb client but local port from the device view */
    local_port = atoi(params);

    params = strstr(params, "tcp:");
    if (params == NULL) {
        return NULL;
    }

    remote_port = atoi(&params[4]);

    if (!local_port || !remote_port) {
        tcp_error_printf(p, "invalid ports %d / %d", local_port, remote_port);
        return NULL;
    }

    /* Check that local port does not already exist */
    adb_list_foreach(&client->reverse_servers, server,
                     atcp_reverse_server_t, server.entry) {
        if (local_port == server->server.local_port) {
            tcp_error_printf(p, "port %d used", local_port);
            return NULL;
        }
    }

    server = (atcp_reverse_server_t*)malloc(sizeof(atcp_reverse_server_t));

    if (server == NULL) {
        return NULL;
    }

    server->server.local_port = local_port;
    server->server.remote_port = remote_port;

    ret = adb_hal_server_listen(client, &server->socket, local_port,
                                tcp_reverse_on_connect_cb);
    if (ret) {
        adb_log("listen failed %d\n", ret);
        free(server);
        tcp_error_printf(p, "Failed to listen on port %d", local_port);
        return NULL;
    }
    /* Register reverse service and fake a one shot service */
    adb_register_reverse_server(client, &server->server);
    p->write_len = sprintf((char*)p->data, "OKAY%04x%d", 4, local_port);
    return NULL;
}

/* Active reverse socket allocation */

adb_tcp_socket_t* tcp_allocate_rstream_socket(adb_client_t *client)
{
    atcp_rstream_service_t *service;

    service = (atcp_rstream_service_t*)malloc(sizeof(atcp_rstream_service_t));
    if (service == NULL) {
        return NULL;
    }

    service->stream_service.client = client;
    service->stream_service.state = F_NOT_CONNECTED; // required ??
    return &service->stream_service.socket;
}

void tcp_release_rstream_socket(adb_tcp_socket_t *socket)
{
    free(socket);
}

/* Reverse list service */

typedef struct atcp_reverse_list_service_s {
    adb_service_t service;
    unsigned int index;
    unsigned int length;
    char data[0];
} atcp_reverse_list_service_t;

static int atcp_reverse_list_on_write(adb_service_t *service, apacket *p) {
    UNUSED(service);
    UNUSED(p);
    return -1;
}

static int atcp_reverse_list_on_ack(adb_service_t *service, apacket *p) {
    atcp_reverse_list_service_t *svc =
        container_of(service, atcp_reverse_list_service_t, service);

    p->write_len = min(svc->length - svc->index, CONFIG_ADBD_PAYLOAD_SIZE);

    /* Last frame ack */
    if (p->write_len == 0)
        return -1;

    memcpy(p->data, &svc->data[svc->index], p->write_len);
    svc->index += p->write_len;
    return 0;
}

static void atcp_reverse_list_on_close(struct adb_service_s *service) {
    free(service);
}

static const adb_service_ops_t atcp_reverse_list_ops = {
    .on_write_frame = atcp_reverse_list_on_write,
    .on_ack_frame   = atcp_reverse_list_on_ack,
    .on_kick        = NULL,
    .on_close       = atcp_reverse_list_on_close
};

static int size_10(uint16_t port)
{
    if (port >= 10000)
        return 5;
    if (port >= 1000)
        return 4;
    if (port >= 100)
        return 3;
    return (port >= 10) ? 2 : 1;
}

static adb_service_t* tcp_reverse_list_service(adb_client_t *client, apacket *p)
{
    adb_reverse_server_t *server;
    atcp_reverse_list_service_t *rl_service;
    unsigned int ret_len = 0;
    char *data;

    if (adb_list_empty(&client->reverse_servers)) {
        p->write_len = sprintf((char*)p->data, "0000");
        return NULL;
    }

    /* Go through reverse list to compute returned message length */
    adb_list_foreach(&client->reverse_servers, server,
                     adb_reverse_server_t, entry) {
        /* Substract trailing zero (-1) and the two %d (-4) */
        ret_len += sizeof(REVERSE_LIST_FMT) - 1 - 4 +
                   size_10(server->local_port) +
                   size_10(server->remote_port);
    }

    /* Allocate reverse list service with space for content,
     * +1 for sprintf() trailing '\0'
     */
    rl_service = (atcp_reverse_list_service_t*)malloc(
                 sizeof(atcp_reverse_list_service_t) + ret_len + 1);

    if (rl_service == NULL) {
        adb_log("failed to allocate service (content len=%d)\n", ret_len);
        return NULL;
    }

    rl_service->service.ops = &atcp_reverse_list_ops;
    rl_service->index = 0;
    rl_service->length = ret_len;

    /* Go through reverse list to generate message content */
    data = rl_service->data;
    adb_list_foreach(&client->reverse_servers, server,
                     adb_reverse_server_t, entry) {
        data += sprintf(data, REVERSE_LIST_FMT,
                        server->local_port, server->remote_port);
    }

    p->write_len = sprintf((char*)p->data, "%04x", ret_len);
    return &rl_service->service;
}

/* TCP reverse server kill service */

static adb_service_t* tcp_reverse_kill_service(adb_client_t *client, const char *params, apacket *p)
{
    int local_port;

    params += 11; /* Skip "killforward" */
    if (!strncmp(params, "-all", 4)) {
        adb_reverse_server_t *server;

        p->write_len = sprintf((char*)p->data, "OKAY");

        while (!adb_list_empty(&client->reverse_servers)) {
            server = container_of(client->reverse_servers.next, adb_reverse_server_t, entry);
            adb_log("stop reverse server tcp:%d <-> tcp:%d\n",
                    server->local_port, server->remote_port);
            adb_reverse_server_close(client, server);
        }
        return NULL;
    }

    if (!strncmp(params, ":tcp:", 5)) {
        adb_reverse_server_t *server;

        params += 5; /* Skip ":tcp:" */
        local_port = atoi(params);

        adb_list_foreach(&client->reverse_servers, server,
                         adb_reverse_server_t, entry) {
            if (server->local_port == local_port) {
                adb_log("stop reverse server tcp:%d <-> tcp:%d\n",
                        server->local_port, server->remote_port);
                adb_reverse_server_close(client, server);
                // TODO return???
                return NULL;
            }
        }

        tcp_error_printf(p, "cannot remove port %d", local_port);
        return NULL;
    }

    return NULL;
}

/* TCP reverse service */

adb_service_t* tcp_reverse_service(adb_client_t *client, const char *params, apacket *p)
{
    /* Skip "reverse:" */
    params += 8;

    adb_log("start reverse service <%s>\n", params);

    if (!strncmp(params, "list-forward", 12)) {
        return tcp_reverse_list_service(client, p);
    }

    if (!strncmp(params, "killforward", 11)) {
        return tcp_reverse_kill_service(client, params, p);
    }

    if (!strncmp(params, "forward:tcp:", 12)) {
        return tcp_reverse_create_service(client, params, p);
    }

    return NULL;
}


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

#include "adb.h"
#include "tcp_service.h"
#include "hal/hal_uv_priv.h"

enum stream_state {
    F_ERROR_CLOSE = 0,
    F_NOT_CONNECTED,
    F_NOTIFY_CLIENT, // forward only (we must send OKAY packet)
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

typedef struct atcp_fstream_service_s {
    adb_stream_service_t stream_service;
    adb_tcp_conn_t conn;
} atcp_fstream_service_t;

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

static void tcp_stream_on_data_cb(adb_tcp_socket_t *socket, apacket *p) {
    adb_stream_service_t *svc;

    svc = container_of(socket, adb_stream_service_t, socket);

    if (svc->state != F_CONNECTED) {
        adb_err("Invalid service state %d\n", svc->state);
        goto exit_close_service;
    }

    if (p->msg.data_length <= 0) {
        adb_err("tcp stream error %d\n", p->msg.data_length);
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
        adb_err("adb_hal_socket_write failed (ret=%d, errno=%d)\n", ret, errno);
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
        adb_err("connect failed (%d)\n", status);
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
        adb_err("Failed to connect to port %d (ret=%d, errno=%d)\n",
                port, ret, errno);
        free(fsvc);
        return NULL;
    }

    fsvc->stream_service.service.ops = &atcp_stream_ops;

    /* Do not send OKAY now but wait for connect callback. */
    p->write_len = APACKET_SERVICE_INIT_ASYNC;
    return &fsvc->stream_service.service;
}

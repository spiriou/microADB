/*
 * Copyright (C) 2020 Simon Piriou. All rights reserved.
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
#include <unistd.h> // write
#include <string.h> // reverse
#include <stdint.h>

#include "adb.h"
#include "tcp_service.h"

typedef struct atcp_rstream_service_s {
    adb_rstream_service_t rsvc;
    uint8_t is_connected;
} atcp_rstream_service_t;

typedef struct atcp_fstream_service_s {
    adb_fstream_service_t fsvc;
} atcp_fstream_service_t;

typedef struct atcp_server_service_s {
    adb_service_t service;
    adb_tcp_server_t socket;
    uint16_t local_port;
    uint16_t remote_port;
} atcp_server_service_t;


static int atcp_server_on_ack(adb_service_t *service, apacket *p);
static int atcp_server_on_write(adb_service_t *service, apacket *p);
static void atcp_server_close(struct adb_service_s *service);

static int atcp_rstream_on_ack(adb_service_t *service, apacket *p);
static int atcp_fstream_on_ack(adb_service_t *service, apacket *p);
static int atcp_stream_on_write(adb_service_t *service, apacket *p);
static void atcp_rstream_close(struct adb_service_s *service);
static void atcp_fstream_close(struct adb_service_s *service);

static const adb_service_ops_t atcp_server_ops = {
    .on_write_frame = atcp_server_on_write,
    .on_ack_frame   = atcp_server_on_ack,
    .on_kick        = NULL,
    .close          = atcp_server_close
};

static const adb_service_ops_t atcp_rstream_ops = {
    .on_write_frame = atcp_stream_on_write,
    .on_ack_frame   = atcp_rstream_on_ack,
    .on_kick        = NULL, // atcp_stream_on_kick,
    .close          = atcp_rstream_close
};

static const adb_service_ops_t atcp_fstream_ops = {
    .on_write_frame = atcp_stream_on_write,
    .on_ack_frame   = atcp_fstream_on_ack,
    .on_kick        = NULL, // atcp_stream_on_kick,
    .close          = atcp_fstream_close
};

static int atcp_server_on_write(adb_service_t *service, apacket *p) {
    int ret;
    atcp_server_service_t *svc = container_of(service, atcp_server_service_t, service);

    UNUSED(svc);
    adb_log("entry\n");

    ret = sprintf((char*)p->data, "ok got %d bytes\n", p->msg.data_length);
    if (ret > 0) {
        p->write_len = ret + 1;
    }
    return 0;

}

static int atcp_server_on_ack(adb_service_t *service, apacket *p) {
    UNUSED(service);
    UNUSED(p);
    adb_log("entry\n");
    return -1;
}

static void atcp_server_close(struct adb_service_s *service) {
    atcp_server_service_t *svc = container_of(service, atcp_server_service_t, service);
    UNUSED(svc);
    adb_log("entry\n");
}

static void tcp_stream_on_data_cb(adb_tcp_socket_t *socket, apacket *p) {
    adb_fstream_service_t *service;

    adb_log("entry\n");

    service = container_of(socket, adb_fstream_service_t, stream.socket);
    adb_log("service %p packet %p client %p\n", service, p, service->client);

    if (p->msg.data_length <= 0) {
        adb_log("ERROR %d\n", p->msg.data_length);
        adb_service_close(service->client, &service->service, p);
        return;
    }

    adb_hal_socket_stop(socket);

    p->write_len = p->msg.data_length;
    p->msg.arg0 = service->service.id;
    p->msg.arg1 = service->service.peer_id;
    adb_client_send_service_payload(service->client, p);
}

static int atcp_rstream_on_ack(adb_service_t *service, apacket *p) {
    UNUSED(p);
    atcp_rstream_service_t *svc =
        container_of(service, atcp_rstream_service_t, rsvc.service);

    adb_log("entry %d\n", svc->is_connected);

    // if (!svc->is_connected) {
        svc->is_connected = 1;
        adb_hal_socket_start(&svc->rsvc.stream.socket, tcp_stream_on_data_cb);
    // }
    return 0;
}

static int atcp_fstream_on_ack(adb_service_t *service, apacket *p) {
    UNUSED(service);
    UNUSED(p);
    return 0;
}

static void stream_send_data_frame_cb(adb_client_t *client, adb_tcp_socket_t *socket, apacket *p) {
    adb_fstream_service_t *service =
        container_of(socket, adb_fstream_service_t, stream.socket);

    adb_log("entry %p\n", p);
    /* TODO is closing */
    if (p) {
        // adb_service_on_async_process_complete(client, &service->service, p);
        send_okay_frame(client, p, service->service.id, service->service.peer_id);
    }
}

static int atcp_stream_on_write(adb_service_t *service, apacket *p) {
    adb_log("entry\n");

    int ret;
    adb_fstream_service_t *svc =
        container_of(service, adb_fstream_service_t, service);

    p->write_len = p->msg.data_length;
    ret = adb_hal_socket_write(&svc->stream.socket, p, stream_send_data_frame_cb);

    if (ret < 0) {
        adb_log("FAIL %d %d\n", ret, errno);
        return -1;
    }
    /* Notify client packet requires async processing */
    return 1;
}

static void atcp_stream_release(adb_tcp_socket_t *socket) {
    atcp_fstream_service_t *service =
        container_of(socket, atcp_fstream_service_t, fsvc.stream.socket);
    adb_log("free service %p\n", service);
    free(service);
}

static void atcp_fstream_close(struct adb_service_s *service) {
    atcp_fstream_service_t *svc =
        container_of(service, atcp_fstream_service_t, fsvc.service);
    adb_hal_socket_close(&svc->fsvc.stream.socket, atcp_stream_release);
}

static void atcp_rstream_close(struct adb_service_s *service) {
    atcp_rstream_service_t *svc =
        container_of(service, atcp_rstream_service_t, rsvc.service);
    adb_hal_socket_close(&svc->rsvc.stream.socket, atcp_stream_release);
}

adb_service_t* tcp_forward_service(adb_client_t *client, const char *params)
{
    int port;
    atcp_fstream_service_t *service;

    port = atoi(&params[4]);
    if (port == 0) {
        return NULL;
    }

    service = (atcp_fstream_service_t*)malloc(sizeof(atcp_fstream_service_t));

    if (service == NULL) {
        return NULL;
    }

    service->fsvc.client = client;

    adb_log("OPEN PORT %d\n", port);
    int ret = adb_hal_socket_connect(client,
        &service->fsvc.stream,
        port,
        tcp_stream_on_data_cb);
    adb_log("RET %d\n", ret);

    service->fsvc.service.ops = &atcp_fstream_ops;

    return &service->fsvc.service;
}

adb_service_t* tcp_reverse_service(adb_client_t *client, const char *params, apacket *p)
{
    int ret;
    int local_port, remote_port;
    atcp_server_service_t *service;

    params += 8; // Skip "reverse:"
    if (strncmp(params, "forward:tcp:", 12)) {
        return NULL;
    }

    params += 12; // Skip "forward:tcp:"

    local_port = atoi(params);
    adb_log("GOT PORT %d\n", local_port);

    params = strstr(params, "tcp:");
    if (params == NULL) {
        return NULL;
    }

    remote_port = atoi(&params[4]);
    adb_log("GOT PORT2 %d\n", remote_port);

    if (!local_port || !remote_port) {
        return NULL;
    }

    service = (atcp_server_service_t*)malloc(sizeof(atcp_server_service_t));

    if (service == NULL) {
        return NULL;
    }

    service->local_port = local_port;
    service->remote_port = remote_port;

    ret = adb_hal_socket_listen(client, &service->socket, local_port);
    if (ret) {
        adb_log("listen failed %d\n", ret);
        free(service);
        return NULL;
    }
    service->service.ops = &atcp_server_ops;

    p->write_len = sprintf((char*)p->data, "OKAY%04d%d", 4, local_port);
    return &service->service;
}

adb_rstream_service_t* tcp_allocate_rstream_service(adb_client_t *client)
{
    atcp_rstream_service_t *service;

    service = (atcp_rstream_service_t*)malloc(sizeof(atcp_rstream_service_t));
    adb_log("allocated %p\n", service);

    if (service == NULL) {
        return NULL;
    }

    service->rsvc.service.ops = &atcp_rstream_ops;
    service->rsvc.client = client;
    service->is_connected = 0;

    adb_register_service(&service->rsvc.service, client);
    return &service->rsvc;
}

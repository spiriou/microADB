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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <unistd.h>

#include "adb.h"
#include "shell_service.h"

typedef struct ash_service_s {
    adb_service_t service;
    adb_pipe_t pipe;
    adb_client_t *client;
    char **argv;
} ash_service_t;

/* Frame processing */
static void shell_on_data_available(adb_pipe_t* pipe, apacket* p);

static int shell_on_ack(adb_service_t *service, apacket *p);
static int shell_on_write(adb_service_t *service, apacket *p);
static void shell_close(struct adb_service_s *service);

static void shell_on_data_available(adb_pipe_t* pipe, apacket* p) {
    // adb_log("entry %p %p\n", p, pipe);

    ash_service_t *service = container_of(pipe, ash_service_t, pipe);

#if 0
    if (p == NULL) {
        p = adb_hal_apacket_allocate(service->client);
        adb_service_close(service->client, &service->service, p);
        return;
    }
#endif

    if (p->msg.data_length <= 0) {
        /* Got EOF */
        adb_service_close(service->client, &service->service, p);
        return;
    }

    p->write_len = p->msg.data_length;
    p->msg.arg0 = service->service.id;
    p->msg.arg1 = service->service.peer_id;
    adb_client_send_service_payload(service->client, p);
}

static int shell_on_write(adb_service_t *service, apacket *p) {
    int ret;
    ash_service_t *svc = container_of(service, ash_service_t, service);
    UNUSED(svc);

    if (p->msg.data_length <= 0) {
        return -1;
    }

    ret = adb_hal_pipe_write(&svc->pipe, p->data, p->msg.data_length);
    // adb_log("WRITE %d %d\n", ret, errno);
    return 0;
}

static int shell_on_ack(adb_service_t *service, apacket *p) {
    ash_service_t *svc = container_of(service, ash_service_t, service);
    UNUSED(svc);
    UNUSED(p);
    return 0;
}

static void shell_on_close(adb_pipe_t *pipe) {
    adb_log("entry\n");
    ash_service_t *svc = container_of(pipe, ash_service_t, pipe);
    free(svc->argv);
    free(svc);
}

static void shell_on_kick(adb_service_t *service) {
    // adb_log("entry\n");
    int ret;
    ash_service_t *svc = container_of(service, ash_service_t, service);
    ret = adb_hal_pipe_start(&svc->pipe, shell_on_data_available);

    /* TODO handle return code */
    assert(ret == 0);
}

static void shell_close(adb_service_t *service) {
    ash_service_t *svc = container_of(service, ash_service_t, service);
    adb_hal_pipe_destroy(&svc->pipe, shell_on_close);
}

static const adb_service_ops_t shell_ops = {
    .on_write_frame = shell_on_write,
    .on_ack_frame   = shell_on_ack,
    .on_kick        = shell_on_kick,
    .close          = shell_close
};

adb_service_t* shell_service(adb_client_t *client, const char *params)
{
    UNUSED(params);
    UNUSED(client);
    int ret;
    ash_service_t *service =
        (ash_service_t*)malloc(sizeof(ash_service_t));

    if (service == NULL) {
        return NULL;
    }

    service->client = client;
    service->service.ops = &shell_ops;

    ret = adb_hal_pipe_setup(client, &service->pipe);
    adb_log("PIPE %d %d\n", ret, errno);

    int argv_size = 0;
    const char *target = &params[6];
    adb_log("TARGET %p\n", target);

    if (target[0] != 0) {
        argv_size += strlen(target)+1;
    }

    service->argv = malloc(sizeof(char*)*4+4+3+argv_size);
    service->argv[0] = (char*)&service->argv[4];
    strcpy(service->argv[0], "nsh");
    if (argv_size > 0) {
        service->argv[1] = &((char*)&service->argv[4])[4];
        service->argv[2] = &((char*)&service->argv[4])[7];
        service->argv[3] = NULL;
        strcpy(service->argv[1], "-c");
        strcpy(service->argv[2], target);
    }
    else {
        service->argv[1] = NULL;
    }

    for (int i=0; service->argv[i]; i++) {
        adb_log("START ARG <%s>\n", service->argv[i]);
    }

    ret = adb_hal_exec(service->argv, &service->pipe, shell_on_data_available);
    adb_log("EXEC %d %d\n", ret, errno);

    return &service->service;
}

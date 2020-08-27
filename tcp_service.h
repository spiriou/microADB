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

#ifndef __TCP_SERVICE_H__
#define __TCP_SERVICE_H__

#include "adb.h"

typedef struct adb_rstream_service_s {
    adb_service_t service;
    adb_client_t *client;
    adb_tcp_rstream_t stream;
    /* Track ports for "reverse:list-forward" command */
    uint16_t local_port;
    uint16_t remote_port;
} adb_rstream_service_t;

typedef struct adb_fstream_service_s {
    adb_service_t service;
    adb_client_t *client;
    adb_tcp_fstream_t stream;
} adb_fstream_service_t;

adb_service_t* tcp_forward_service(adb_client_t *client, const char *params);
adb_service_t* tcp_reverse_service(adb_client_t *client, const char *params, apacket *p);

adb_rstream_service_t* tcp_allocate_rstream_service(adb_client_t *client);

#endif /* __TCP_SERVICE_H__ */

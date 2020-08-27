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

#ifndef __ADB_H
#define __ADB_H

#include <limits.h>
#include <sys/types.h>

// FIXME
#include <stdio.h>
#include <stdint.h>
#include <stddef.h> // offsetof
#include <assert.h>

#define CONFIG_ADB_TCP_PORT 5555
#define CONFIG_ADB_TCP_SERVER y

#ifndef CONFIG_PATH_MAX
#define CONFIG_PATH_MAX 256
#endif

#define CONFIG_ADB_CNXN_PAYLOAD_SIZE 1024 // 256
#define CONFIG_ADB_PAYLOAD_SIZE 40 // 72 // 40 // 4096 // 256 // 4096
#define CONFIG_ADB_TOKEN_SIZE 20

#ifndef __NUTTX__
#define CONFIG_SYSTEM_ADB_PRODUCT_NAME   "adb_dev"
#define CONFIG_SYSTEM_ADB_PRODUCT_MODEL  "adb_board"
#define CONFIG_SYSTEM_ADB_PRODUCT_DEVICE "NuttX_device"
#define CONFIG_SYSTEM_ADB_TCP_FRAME_MAX  2

#define UNUSED(x) (void)(x)

#define _info(...) do { \
    fprintf(stderr, "%s: ", __func__); \
    fprintf(stderr, __VA_ARGS__); \
    } while (0)
#endif

#define CONFIG_SYSTEM_ADB_DEVICE_ID "abcd"

#include "hal/hal_uv.h"

#define adb_log(...) _info(__VA_ARGS__)

#define container_of(ptr, type, member) \
  ((type *)((uintptr_t)(ptr) - offsetof(type, member)))

#define fatal(...) assert(0)

#define A_SYNC 0x434e5953
#define A_CNXN 0x4e584e43
#define A_OPEN 0x4e45504f
#define A_OKAY 0x59414b4f
#define A_CLSE 0x45534c43
#define A_WRTE 0x45545257
#define A_AUTH 0x48545541

/* AUTH packets first argument */
/* Request */
#define ADB_AUTH_TOKEN         1
/* Response */
#define ADB_AUTH_SIGNATURE     2
#define ADB_AUTH_RSAPUBLICKEY  3

#define CS_ANY       -1
#define CS_OFFLINE    0
#define CS_BOOTLOADER 1
#define CS_DEVICE     2
#define CS_HOST       3
#define CS_RECOVERY   4
#define CS_NOPERM     5 /* Insufficient permissions to communicate with the device */
#define CS_SIDELOAD   6
#define CS_UNAUTHORIZED 7

// ADB protocol version.
#define A_VERSION 0x01000000

// Used for help/version information.
#define ADB_VERSION_MAJOR 1
#define ADB_VERSION_MINOR 0

// Increment this when we want to force users to start a new adb server.
#define ADB_SERVER_VERSION 32

struct adb_client_s;
struct adb_service_s;
struct apacket_s;

struct adb_tcp_socket_s;
struct adb_tcp_stream_s;
struct adb_tcp_fstream_s;
struct adb_tcp_rstream_s;
struct adb_tcp_server_s;

typedef struct adb_pipe_s adb_pipe_t;
typedef struct adb_tcp_socket_s adb_tcp_socket_t;
typedef struct adb_tcp_server_s adb_tcp_server_t;
typedef struct adb_tcp_fstream_s adb_tcp_stream_t;
typedef struct adb_tcp_fstream_s adb_tcp_fstream_t;
typedef struct adb_tcp_rstream_s adb_tcp_rstream_t;

// typedef void (*adb_send_cb)(struct adb_client_s*, struct apacket_s*);

typedef struct amessage_s {
    unsigned command;       /* command identifier constant      */
    unsigned arg0;          /* first argument                   */
    unsigned arg1;          /* second argument                  */
    unsigned data_length;   /* length of payload (0 is allowed) */
    unsigned data_check;    /* checksum of data payload         */
    unsigned magic;         /* command ^ 0xffffffff             */
} amessage;

typedef struct apacket_s
{
    // struct apacket_s *next;
    // unsigned len;
    // unsigned char *ptr;
    unsigned int write_len;
    // adb_send_cb send_cb;

    amessage msg;
    uint8_t data[CONFIG_ADB_PAYLOAD_SIZE];
} apacket;

typedef struct adb_service_ops_s {
    int (*on_write_frame)(struct adb_service_s *service, apacket *p);
    int (*on_ack_frame)(struct adb_service_s *service, apacket *p);
    void (*on_kick)(struct adb_service_s *service);
    void (*close)(struct adb_service_s *service);
} adb_service_ops_t;

typedef struct adb_reverse_service_ops_s {
    adb_service_ops_t svc_ops;
    uint16_t (*get_port)(struct adb_service_s*, uint8_t is_local);
} adb_reverse_service_ops_t;

typedef struct adb_service_s {
    /* chain pointers for the local/remote list of
    ** asockets that this asocket lives in
    */
    struct adb_service_s *next;
    const adb_service_ops_t *ops;

    /* the unique identifier for this service */
    int id;
    int peer_id;
    // struct adb_client_s *client;
} adb_service_t;

typedef struct adb_client_ops_s {
    int (*write)(struct adb_client_s *client, apacket *p);
    void (*kick)(struct adb_client_s *client);
    void (*close)(struct adb_client_s *client);
} adb_client_ops_t;

typedef struct adb_client_s {
    struct adb_client_s *next;
    const adb_client_ops_t *ops;
    int next_service_id;
    adb_service_t *services;
    adb_service_t *r_services;
    // adb_tcp_socket_t *sockets;
    uint8_t is_connected;
#ifdef CONFIG_SYSTEM_ADB_AUTHENTICATION
    uint8_t token[CONFIG_ADB_TOKEN_SIZE];
#endif
} adb_client_t;

typedef struct adb_context_s {
    adb_client_t *clients;
} adb_context_t;

/* adb HAL */

adb_context_t* adb_hal_create_context(void);
void adb_hal_destroy_context(adb_context_t *context);
int adb_hal_run(adb_context_t *context);

int adb_hal_random(void *buf, size_t len);

/* Client */

adb_client_t* adb_create_client(size_t size);
void adb_destroy_client(adb_client_t *client);
void adb_client_kick_services(adb_client_t *client);

adb_client_t* adb_hal_create_client(size_t size);
void adb_hal_destroy_client(adb_client_t *client);

void adb_client_send_service_payload(adb_client_t *client, apacket *p);

/* Services */

void adb_register_service(adb_service_t *svc, adb_client_t *client);
void send_okay_frame(adb_client_t *client, apacket *p,
    unsigned local, unsigned remote);
void send_open_frame(adb_client_t *client, apacket *p,
    unsigned local, unsigned remote, int size);
void adb_service_on_async_process_complete(adb_client_t *client,
    adb_service_t *service, apacket *p);
void adb_service_close(adb_client_t *client, adb_service_t *svc, apacket *p);

/* TODO */

// apacket* adb_hal_apacket_allocate(adb_client_t *client);
void adb_hal_apacket_release(adb_client_t *client, apacket *p);

int adb_fill_connect_data(char *buf, size_t bufsize);
void adb_process_packet(adb_client_t *client, apacket *p);
int adb_check_frame_data(apacket *p);
int adb_check_frame_header(apacket *p);
int adb_check_auth_frame_header(apacket *p);

int adb_hal_socket_connect(adb_client_t *client, adb_tcp_fstream_t *stream,
    int port, void (*on_data_cb)(adb_tcp_socket_t*, apacket*));
int adb_hal_socket_listen(adb_client_t *client, adb_tcp_server_t *socket, int port);
void adb_hal_socket_close(adb_tcp_socket_t *socket, void (*close_cb)(adb_tcp_socket_t*));

int adb_hal_socket_start(adb_tcp_socket_t *socket,
    void (*on_data_cb)(adb_tcp_socket_t*, apacket*));
int adb_hal_socket_stop(adb_tcp_socket_t *socket);
int adb_hal_socket_write(adb_tcp_socket_t *socket, apacket *p,
    void (*cb)(adb_client_t*, adb_tcp_socket_t*, apacket*));

int adb_hal_pipe_setup(adb_client_t *client, adb_pipe_t *pipe);
int adb_hal_pipe_start(adb_pipe_t *pipe, void (*on_data_cb)(adb_pipe_t*, apacket*));
void adb_hal_pipe_destroy(adb_pipe_t *pipe, void (*close_cb)(adb_pipe_t*));
int adb_hal_pipe_write(adb_pipe_t *pipe, const void *buf, size_t count);
int adb_hal_exec(char * const argv[], adb_pipe_t *pipe, void (*on_data_cb)(adb_pipe_t*, apacket*));
void DumpHex(const void* data, size_t size);

#ifdef CONFIG_SYSTEM_ADB_AUTHENTICATION
extern const unsigned char *g_adb_public_keys[];
#endif

#endif /* __ADB_H */

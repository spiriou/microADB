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

#ifndef __ADB_HAL_UV_PRIV_H__
#define __ADB_HAL_UV_PRIV_H__

#include <uv.h>
#include "adb.h"

/****************************************************************************
 * Public types
 ****************************************************************************/

typedef struct apacket_uv_s
{
    uv_write_t wr;
    void *priv;
    apacket p;
} apacket_uv_t;

typedef struct adb_client_uv_s {
    adb_client_t client;
    /* Frame allocation management */
    struct apacket_uv_s *cur_packet;
    unsigned int cur_len;
    int frame_count;
    /* Events handling: the next field must be libuv handle */
} adb_client_uv_t;

#define adb_uv_get_client_handle(_c) \
    ((uv_handle_t*)((adb_client_uv_t*)(_c)+1))

typedef struct adb_context_uv_s {
    adb_context_t context;
    uv_loop_t *loop;
#ifdef CONFIG_ADBD_TCP_SERVER
    uv_tcp_t tcp_server;
#endif
} adb_context_uv_t;

#ifdef CONFIG_ADBD_SOCKET_SERVICE
struct adb_tcp_socket_s {
    uv_tcp_t handle;
    void (*close_cb)(struct adb_tcp_socket_s*);
    void (*on_data_cb)(struct adb_tcp_socket_s*, struct apacket_s*);
    void (*on_write_cb)(struct adb_client_s*, struct adb_tcp_socket_s*, struct apacket_s*, bool);
};

struct adb_tcp_conn_s {
    uv_connect_t connect_req;
    void (*on_connect_cb)(adb_tcp_socket_t*, int);
};
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_ADBD_TCP_SERVER
int adb_uv_tcp_setup(adb_context_uv_t *adbd);
#endif

#ifdef CONFIG_ADBD_USB_SERVER
int adb_uv_usb_setup(adb_context_uv_t *adbd, const char *path);
#endif

/* hal packet management */

apacket_uv_t* adb_uv_packet_allocate(adb_client_uv_t *client,
                                     int before_connect);
void adb_uv_packet_release(adb_client_uv_t *c, apacket_uv_t *p);

void adb_uv_allocate_frame(adb_client_uv_t *client, uv_buf_t* buf);

/* hal stream helpers */

void adb_uv_after_write(uv_write_t* req, int status);
void adb_uv_on_data_available(adb_client_uv_t *client, uv_stream_t *stream,
        ssize_t nread, const uv_buf_t* buf);

/* hal client management */

adb_client_uv_t* adb_uv_create_client(size_t size);
void adb_uv_close_client(adb_client_uv_t *client);

#endif /* __ADB_HAL_UV_PRIV_H__ */

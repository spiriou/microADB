/*
 * Copyright (C) 2024 Xiaomi Inc. All rights reserved.
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

#include <fcntl.h>
#include <unistd.h>

#include "adb.h"
#include "hal_uv_priv.h"
#include <uv.h>

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

/****************************************************************************
 * Private types
 ****************************************************************************/

typedef struct adb_client_qemu_s {
    adb_client_uv_t uc;
    /* libuv handle must be right after adb_client_uv_t */
    uv_pipe_t pipe;
} adb_client_qemu_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void qemu_uv_allocate_frame(uv_handle_t *handle,
                       size_t suggested_size, uv_buf_t *buf) {
    UNUSED(suggested_size);
    adb_client_qemu_t *client = container_of(handle, adb_client_qemu_t, pipe);

    adb_uv_allocate_frame(&client->uc, buf);
}

static void qemu_uv_on_data_available(uv_stream_t *handle,
        ssize_t nread, const uv_buf_t *buf) {
    adb_client_qemu_t *client = container_of(handle, adb_client_qemu_t, pipe);

    adb_uv_on_data_available(&client->uc, handle, nread, buf);
}

static int qemu_uv_write(adb_client_t *c, apacket *p) {
    int ret;
    uv_buf_t buf;
    apacket_uv_t *up = container_of(p, apacket_uv_t, p);
    adb_client_qemu_t *client = container_of(c, adb_client_qemu_t, uc.client);

    buf = uv_buf_init((char *)&p->msg,
        sizeof(p->msg) + p->msg.data_length);

    /* Packet is now tracked by libuv */
    up->wr.data = &client->uc;

    ret = uv_write(&up->wr, (uv_stream_t *)&client->pipe, &buf, 1,
        adb_uv_after_write);
    if (ret < 0) {
        adb_err("uv_write failed %d %d\n", ret, errno);
        /* Caller will destroy client */
        return ret;
    }

    return 0;
}

static void qemu_uv_kick(adb_client_t *c) {
    adb_client_qemu_t *client = container_of(c, adb_client_qemu_t, uc.client);

    if (!uv_is_active((uv_handle_t *)&client->pipe)) {
        int ret = uv_read_start((uv_stream_t *)&client->pipe,
            qemu_uv_allocate_frame,
            qemu_uv_on_data_available);
        /* TODO check return code */
        assert(ret == 0);
    }

    adb_client_kick_services(c);
}

static void qemu_uv_on_close(uv_handle_t *handle) {
    adb_client_qemu_t *client = container_of(handle, adb_client_qemu_t, pipe);

    adb_uv_close_client(&client->uc);
}

static void qemu_uv_close(adb_client_t *c) {
    adb_client_qemu_t *client = container_of(c, adb_client_qemu_t, uc.client);

    /* Close pipe and cancel all pending write requests if any */
    uv_close((uv_handle_t *)&client->pipe, qemu_uv_on_close);
}

static const adb_client_ops_t adb_qemu_uv_ops = {
    .write = qemu_uv_write,
    .kick  = qemu_uv_kick,
    .close = qemu_uv_close
};

/* Please refer to:
 * https://android.googlesource.com/platform/system/core/+/refs/heads/android11-dev/adb/daemon/transport_qemu.cpp#59
 * https://android.googlesource.com/platform/system/core/+/refs/heads/android11-dev/qemu_pipe/qemu_pipe.cpp#37
 * for qemu adb pipe protocol details.
 */

static void qemu_uv_on_setup(uv_handle_t *server) {
    adb_context_uv_t *adbd = (adb_context_uv_t *)server->data;

    adb_uv_qemu_setup(adbd);
}

static void qemu_uv_on_readable(uv_poll_t *server, int status, int events) {
    int ret;
    char buf[2];
    uv_os_fd_t fd;
    adb_client_qemu_t *client;
    adb_context_uv_t *adbd = (adb_context_uv_t *)server->data;

    ret = uv_fileno((uv_handle_t *)server, &fd);
    assert(ret == 0);
    uv_close((uv_handle_t *)server, qemu_uv_on_setup);

    if (status < 0 || (events & UV_DISCONNECT)) {
        adb_err("connect failed %d %d\n", status, events);
        goto err;
    }

    ret = read(fd, buf, sizeof(buf));
    if (ret != sizeof(buf)) {
        adb_err("read failed %d %d\n", ret, errno);
        goto err;
    }

    if (buf[0] != 'o' || buf[1] != 'k') {
        adb_err("handshake failed\n");
        goto err;
    }

    ret = write(fd, "start", 5);
    if (ret != 5) {
        adb_err("write failed %d %d\n", ret, errno);
        goto err;
    }

    client = (adb_client_qemu_t *)adb_uv_create_client(sizeof(*client));
    if (client == NULL) {
        adb_err("failed to allocate client\n");
        goto err;
    }

    /* Setup adb_client */
    client->uc.client.ops = &adb_qemu_uv_ops;

    ret = uv_pipe_init(adbd->loop, &client->pipe, 0);
    /* TODO check return code */
    assert(ret == 0);

    ret = uv_pipe_open(&client->pipe, fd);
    /* TODO check return code */
    assert(ret == 0);

    ret = uv_read_start((uv_stream_t *)&client->pipe,
        qemu_uv_allocate_frame,
        qemu_uv_on_data_available);
    /* TODO check return code */
    assert(ret == 0);
    return;

err:
    close(fd);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int adb_uv_qemu_setup(adb_context_uv_t *adbd) {
    const char cookie[] = "pipe:qemud:adb:"
                          STR(CONFIG_ADBD_QEMU_SERVER_PORT)
                          "\0accept";
    int ret;
    int fd;

    fd = open("/dev/goldfish_pipe", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        adb_err("qemu server open error %d %d\n", fd, errno);
        return fd;
    }

    ret = write(fd, cookie, sizeof(cookie) - 1);
    if (ret != sizeof(cookie) - 1) {
        adb_err("qemu server write error %d %d\n", ret, errno);
        goto err;
    }

    ret = uv_poll_init(adbd->loop, &adbd->qemu_server, fd);
    adbd->qemu_server.data = adbd;
    if (ret < 0) {
        adb_err("qemu server init error %d %d\n", ret, errno);
        goto err;
    }

    ret = uv_poll_start(&adbd->qemu_server, UV_READABLE, qemu_uv_on_readable);
    if (ret < 0) {
        adb_err("qemu server start error %d %d\n", ret, errno);
        qemu_uv_on_readable(&adbd->qemu_server, ret, 0);
    }

    return 0;

err:
    close(fd);
    return ret;
}

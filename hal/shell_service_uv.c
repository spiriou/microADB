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

#define _DEFAULT_SOURCE 1 /* Force _DEFAULT_SOURCE (required for cfmakeraw) */
#define _XOPEN_SOURCE 600 /* Force _XOPEN_SOURCE (required for pty features) */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "adb.h"
#include "hal_uv_priv.h"
#include "shell_service.h"

/****************************************************************************
 * Private types
 ****************************************************************************/

typedef struct ash_service_s {
    adb_service_t service;
    uv_pipe_t shell_pipe;
    uv_process_t process;
    int wait_ack;
} ash_service_t;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifndef O_CLOEXEC
static int shell_set_cloexec(int fd);
#endif

static void on_child_exit(uv_process_t *process, int64_t exit_status,
                          int term_signal);

static void alloc_buffer(uv_handle_t *handle, size_t len, uv_buf_t *buf);
static void pipe_on_data_available(uv_stream_t* stream, ssize_t nread,
                                   const uv_buf_t* buf);

static int shell_write(adb_service_t *service, apacket *p);
static void shell_after_write(uv_write_t* req, int status);

static int shell_ack(adb_service_t *service, apacket *p);
static void shell_close(struct adb_service_s *service);
static void shell_kick(adb_service_t *service);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifndef O_CLOEXEC
static int shell_set_cloexec(int fd) {
    int ret;

    if ((ret = fcntl(fd, F_GETFD)) < 0) {
        return ret;
    }

    ret = ret | FD_CLOEXEC;

    if ((ret = fcntl(fd, F_SETFD, ret)) < 0) {
        return ret;
    }

    return 0;
}
#endif

static void on_child_exit(uv_process_t *process, int64_t exit_status,
        int term_signal) {
    ash_service_t *svc = (ash_service_t*)process->data;

    adb_log("shell %d<->%d exited with status %ld, signal %d\n",
        svc->service.id, svc->service.peer_id,
        exit_status, term_signal);
}

static void alloc_buffer(uv_handle_t *handle, size_t len, uv_buf_t *buf) {
    UNUSED(len);
    apacket_uv_t *ap;
    ash_service_t *service = container_of(handle, ash_service_t, shell_pipe);
    adb_client_uv_t *client = (adb_client_uv_t *)service->shell_pipe.data;

    assert(service->wait_ack == 0);

    ap = adb_uv_packet_allocate(client, 0);
    if (ap == NULL) {
      buf->base = NULL;
      return;
    }

    buf->base = (char*)ap->p.data;
    buf->len = CONFIG_ADBD_PAYLOAD_SIZE;
}

static void pipe_on_data_available(uv_stream_t* stream, ssize_t nread,
        const uv_buf_t* buf) {
    ash_service_t *service = container_of(stream, ash_service_t, shell_pipe);
    adb_client_uv_t *client = (adb_client_uv_t *)service->shell_pipe.data;
    apacket *p;

    if (nread == UV_ENOBUFS) {
        /* No frame available, stop read events for now */
        uv_read_stop((uv_stream_t*)&service->shell_pipe);
        return;
    }

    p = container_of(buf->base, apacket, data);

    if (nread <= 0) {
        if (nread != UV_EOF) {
            adb_log("GOT ERROR nread %d\n", nread);
        }
        adb_service_close(&client->client, &service->service, p);
        return;
    }

    /* Wait for ACK before processing next frame from shell */

    service->wait_ack = 1;
    uv_read_stop((uv_stream_t*)&service->shell_pipe);

    p->write_len = nread;
    p->msg.arg0 = service->service.id;
    p->msg.arg1 = service->service.peer_id;
    adb_send_data_frame(&client->client, p);
}

static void shell_after_write(uv_write_t* req, int status) {
    apacket_uv_t *up = container_of(req, apacket_uv_t, wr);
    ash_service_t *svc = (ash_service_t*)req->data;
    adb_client_uv_t *client = (adb_client_uv_t *)svc->shell_pipe.data;

    if (status < 0) {
        adb_log("uv_write failed %d\n", status);
        adb_service_close(&client->client, &svc->service, &up->p);
        return;
    }

    /* Write frame processing done, send acknowledge frame */
    adb_send_okay_frame(&client->client, &up->p,
        svc->service.id, svc->service.peer_id);
}

static int shell_write(adb_service_t *service, apacket *p) {
    int ret;
    uv_buf_t buf;

    apacket_uv_t *up = container_of(p, apacket_uv_t, p);
    ash_service_t *svc = container_of(service, ash_service_t, service);

    buf = uv_buf_init((char*)&p->data, p->msg.data_length);
    up->wr.data = svc;

    ret = uv_write(&up->wr, (uv_stream_t*)&svc->shell_pipe, &buf, 1,
        shell_after_write);
    if (ret) {
        adb_log("uv_write failed %d %d\n", ret, errno);
        return -1;
    }

    /* Notify ADB client that packet is now managed by service */
    return 1;
}

static int shell_ack(adb_service_t *service, apacket *p) {
    ash_service_t *svc = container_of(service, ash_service_t, service);
    UNUSED(p);

    svc->wait_ack = 0;
    shell_kick(service);
    return 0;
}

static void shell_kick(adb_service_t *service) {
    ash_service_t *svc = container_of(service, ash_service_t, service);

    if (!svc->wait_ack) {
        int ret;
        ret = uv_read_start((uv_stream_t*)&svc->shell_pipe,
            alloc_buffer, pipe_on_data_available);
        /* TODO handle return code */
        assert(ret == 0);
    }
}

static void shell_close_process_callback(uv_handle_t *handle) {
    free(handle->data);
}

static void shell_close_pipe_callback(uv_handle_t *handle) {
    ash_service_t *svc = container_of(handle, ash_service_t, shell_pipe);
    uv_close((uv_handle_t *)&svc->process, shell_close_process_callback);
}

static void shell_close(adb_service_t *service) {
  ash_service_t *svc = container_of(service, ash_service_t, service);

  /* Terminate child process in case it is still running */

  uv_process_kill(&svc->process, SIGKILL);

  uv_close((uv_handle_t *)&svc->shell_pipe, shell_close_pipe_callback);
}

static const adb_service_ops_t shell_ops = {
  .on_write_frame = shell_write,
  .on_ack_frame   = shell_ack,
  .on_kick        = shell_kick,
  .on_close       = shell_close
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

adb_service_t *shell_service(adb_client_t *client, const char *params) {
    int ret;
    char **argv;
    const char *target_cmd;
    uv_process_options_t options;
    uv_stdio_container_t stdio[3];
    struct termios slavetermios;
    char *slavedevice = NULL;
    int fds[2];

    ash_service_t *service =
        (ash_service_t *)malloc(sizeof(ash_service_t));

    if (service == NULL) {
        return NULL;
    }

    service->service.ops = &shell_ops;
    service->shell_pipe.data = client;
    service->process.data = service;
    service->wait_ack = 0;

    target_cmd = &params[sizeof(ADB_SHELL_PREFIX)-1];

    /* Setup child process argv */

    if (target_cmd[0] != 0) {
        /* Build argv: <sh -c "command">
        * argv[0] => "sh"
        * argv[1] => "-c"
        * argv[2] => command
        * argv[3] => NULL
        *
        * malloc content:
        * - 4 argv pointers
        * - x characters: CONFIG_ADBD_SHELL_SERVICE_CMD
        * - 3 characters: "-c\0"
        * - strlen(target)+1: space for command string
        */

        argv = malloc(sizeof(char *) * 4 +
            sizeof(CONFIG_ADBD_SHELL_SERVICE_CMD) +
            3 + (strlen(target_cmd)+1));
        if (argv == NULL) {
            goto exit_free_service;
        }

        argv[0] = (char *)&argv[4];
        argv[1] = argv[0] + sizeof(CONFIG_ADBD_SHELL_SERVICE_CMD);
        argv[2] = argv[1] + 3;
        argv[3] = NULL;
        strcpy(argv[1], "-c");
        strcpy(argv[2], target_cmd);
    }
    else {
        /* Build argv: <sh>
        * argv[0] => "sh"
        * argv[1] => NULL
        */

        argv = malloc(sizeof(char *) * 2 +
                sizeof(CONFIG_ADBD_SHELL_SERVICE_CMD));
        if (argv == NULL) {
            goto exit_free_service;
        }

        argv[0] = (char *)&argv[2];
        argv[1] = NULL;
    }

    strcpy(argv[0], CONFIG_ADBD_SHELL_SERVICE_CMD);

    /* Setup IPC to communicate with child shell process.
     * Create interactive session based on pty */

#ifdef O_CLOEXEC
    fds[0] = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
#else
    fds[0] = posix_openpt(O_RDWR | O_NOCTTY);
#endif

    if (fds[0] < 0) {
        goto exit_free_argv;
    }

    if ((ret = grantpt(fds[0]))) {
        goto exit_release_pipe;
    }
    if ((ret = unlockpt(fds[0]))) {
        goto exit_release_pipe;
    }

    slavedevice = ptsname(fds[0]);

#ifdef O_CLOEXEC
    fds[1] = open(slavedevice, O_RDWR | O_NOCTTY | O_CLOEXEC);
#else
    fds[1] = open(slavedevice, O_RDWR | O_NOCTTY);
#endif
    if (fds[1] < 0) {
        adb_log("slavefd failed (%d)\n", errno);
        goto exit_release_pipe;
    }

#ifndef O_CLOEXEC
    if ((ret = shell_set_cloexec(fds[0])) ||
        (ret = shell_set_cloexec(fds[1]))) {
        goto exit_release_pipe;
    }
#endif

    if (target_cmd[0] != 0) {
        /* This is not an interactive session. Swith to RAW mode */

        cfmakeraw(&slavetermios);
        tcsetattr(fds[1], TCSADRAIN, &slavetermios);
    }

    /* Open pipe endpoint that is managed by adb daemon */

    ret = uv_pipe_init(adb_uv_get_client_handle(client)->loop,
                       &service->shell_pipe, 0);
    if (ret) {
        goto exit_release_pipe;
    }

    ret = uv_pipe_open(&service->shell_pipe, fds[0]);
    if (ret) {
        goto exit_release_pipe;
    }

    /* Spawn new uv_process_t to manage shell */

    memset(&options, 0, sizeof(options));
    memset(stdio, 0, sizeof(stdio));

    options.stdio_count = 3;
    options.stdio = stdio;
    stdio[0].flags = UV_INHERIT_FD | UV_READABLE_PIPE;
    stdio[0].data.fd = fds[1];

    stdio[1].flags = UV_INHERIT_FD | UV_WRITABLE_PIPE;
    stdio[1].data.fd = fds[1];

    stdio[2].flags = UV_INHERIT_FD | UV_WRITABLE_PIPE;
    stdio[2].data.fd = fds[1];

    options.exit_cb = on_child_exit;
    options.file = CONFIG_ADBD_SHELL_SERVICE_PATH;
    options.args = argv;

    /* FIXME Control-C does not work without this */

    options.flags = UV_PROCESS_DETACHED;

    ret = uv_spawn(service->shell_pipe.loop, &service->process, &options);
    if (ret) {
        adb_log("uv_spawn failed (%s)\n", uv_strerror(ret));
        goto exit_release_pipe;
    }

    /* Close shell child process endpoint and free argv array
     * at it is useless now */

    close(fds[1]);
    free(argv);

    /* Start waiting for data from shell process */

    shell_kick(&service->service);

    return &service->service;

exit_release_pipe:
    if (fds[0] >= 0) {
        close(fds[0]);
    }
    if (fds[1] >= 0) {
        close(fds[1]);
    }
exit_free_argv:
    free(argv);
exit_free_service:
    free(service);
    return NULL;
}

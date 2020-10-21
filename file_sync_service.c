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

#include "adb.h"
#include "file_sync_service.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#define min(a,b) ((a) < (b) ? (a):(b))

#define SYNC_TEMP_BUFF_SIZE CONFIG_PATH_MAX // 64

enum {
    AFS_STATE_WAIT_CMD,
    AFS_STATE_WAIT_CMD_DATA,
    AFS_STATE_PROCESS_STAT,
    AFS_STATE_PROCESS_RECV,
    AFS_STATE_PROCESS_LIST,
    AFS_STATE_PROCESS_SEND_FILE_HDR,
    AFS_STATE_PROCESS_SEND_FILE_DATA,
    AFS_STATE_PROCESS_SEND_SYM_HDR,
    AFS_STATE_PROCESS_SEND_SYM_DATA
};

enum {
    AFS_OK = 0,
    AFS_RESET_STATE = -1,
    AFS_ERROR = -2
};

typedef struct afs_service_s {
    adb_service_t service;
    uint8_t *packet_ptr;

    uint8_t state;
    unsigned cmd;
    unsigned namelen;

    union {
        struct {
            DIR *d;
            char *path;
            char *file_ptr;
        } list;

        struct {
            int fd;
        } send_file;

        struct {
            char *path;
        } send_link;

        struct {
            int fd;
        } recv;
    };

    unsigned size;
    char buff[SYNC_TEMP_BUFF_SIZE];
} afs_service_t;

/* Helpers */

static int read_from_packet(afs_service_t *svc, apacket *p, unsigned int size);
static int create_path_directories(char *name);

static void prepare_fail_message(afs_service_t *svc, apacket *p, const char *reason);
static void prepare_fail_errno(afs_service_t *svc, apacket *p);
static void prepare_okay_message(afs_service_t *svc, apacket *p);

/* Generic states for file service */

static int state_wait_cmd(afs_service_t *svc, apacket *p);
static int state_wait_cmd_data(afs_service_t *svc, apacket *p);

/* State init functions */

static int state_init_stat(afs_service_t *svc, apacket *p);
static int state_init_list(afs_service_t *svc, apacket *p);
static int state_init_send(afs_service_t *svc, apacket *p);
static int state_init_send_file(afs_service_t *svc, apacket *p, mode_t mode);
static int state_init_send_link(afs_service_t *svc, apacket *p);
static int state_init_recv(afs_service_t *svc, apacket *p);

/* State process functions */

static int state_process_send_header(afs_service_t *svc, apacket *p);
static int state_process_send_file(afs_service_t *svc, apacket *p);
#ifdef CONFIG_SYSTEM_ADB_FILE_SYMLINK
static int state_process_send_sym(afs_service_t *svc, apacket *p);
#endif
static int state_process_recv(afs_service_t *svc, apacket *p);
static int state_process_list(afs_service_t *svc, apacket *p);

/* Reset service state */

static void state_reset(afs_service_t *svc);

/* Frame processing */

static int file_sync_on_ack(adb_service_t *service, apacket *p);
static int file_sync_on_write(adb_service_t *service, apacket *p);
static void file_sync_close(struct adb_service_s *service);

static void prepare_fail_message(afs_service_t *svc, apacket *p, const char *reason)
{
    UNUSED(svc);
    int len;
    union syncmsg *msg = (union syncmsg*)p->data;

    adb_log("sync: failure: %s\n", reason);

    len = min(strlen(reason),
        CONFIG_ADB_PAYLOAD_SIZE-sizeof(msg->data));
    memcpy((char*)(&msg->data+1), reason, len);

    msg->data.id = ID_FAIL;
    msg->data.size = htoll(len);
    
    p->write_len = sizeof(msg->data) + len;
}

static void prepare_fail_errno(afs_service_t *svc, apacket *p)
{
    prepare_fail_message(svc, p, strerror(errno));
}

static void prepare_okay_message(afs_service_t *svc, apacket *p)
{
    UNUSED(svc);
    union syncmsg *msg = (union syncmsg*)p->data;

    msg->status.id = ID_OKAY;
    msg->status.msglen = 0;
    p->write_len = sizeof(msg->status);
}

static int read_from_packet(afs_service_t *svc, apacket *p, unsigned int size)
{
    int ret = -EINVAL;

    if (svc->size > size || size > SYNC_TEMP_BUFF_SIZE) {
        goto exit_reset;
    }

    unsigned int chunk_size = min(size-svc->size, p->msg.data_length);

    // adb_log("copy %p %p %d %p\n", svc->buff, svc->buff+svc->size, chunk_size, svc->packet_ptr);
    memcpy(svc->buff+svc->size, svc->packet_ptr, chunk_size);

    svc->size += chunk_size;
    p->msg.data_length -= chunk_size;
    svc->packet_ptr += chunk_size;

    if (svc->size == size) {
        ret = 0;
        goto exit_reset;
    }
    return -EAGAIN;

exit_reset:
    svc->size = 0;
    return ret;
}

static int create_path_directories(char *name)
{
    int ret;
    char *x = name;
    unsigned int mode = 0775;

    // D("entry <%s>\n", name);

    if(name[0] != '/') return -1;

    for(;;) {
        // adb_log("loop <%s>\n", x);
        x = strchr(++x, '/');
        if(!x) return 0;
        *x = 0;
        // adb_log("try <%s>\n", name);
        ret = mkdir(name, mode);
        *x = '/';
        if((ret < 0) && (errno != EEXIST)) {
            // adb_log("mkdir(\"%s\") -> %s\n", name, strerror(errno));
            return ret;
        }
    }
    return 0;
}

static void state_reset(afs_service_t *svc)
{
    switch (svc->state) {
        case AFS_STATE_PROCESS_LIST:
            free(svc->list.path);
            closedir(svc->list.d);
            break;

        case AFS_STATE_PROCESS_RECV:
            close(svc->recv.fd);
            break;

        case AFS_STATE_PROCESS_SEND_SYM_HDR:
        case AFS_STATE_PROCESS_SEND_SYM_DATA:
            free(svc->send_link.path);
            break;

        case AFS_STATE_PROCESS_SEND_FILE_HDR:
        case AFS_STATE_PROCESS_SEND_FILE_DATA:
            close(svc->send_file.fd);
            /* TODO handle file unlink if transfer incomplete */
            break;

        default:
            break;
    }

    svc->state = AFS_STATE_WAIT_CMD;
    svc->size = 0;
}

static int state_init_stat(afs_service_t *svc, apacket *p)
{
    struct stat st;
    union syncmsg *msg = (union syncmsg*)p->data;

    msg->stat.id = ID_STAT;

    if(!stat(svc->buff, &st)) {
        msg->stat.mode = htoll(st.st_mode);
        msg->stat.size = htoll(st.st_size);
        msg->stat.time = htoll(st.st_mtime);
    } else {
        /* File not found */
        msg->stat.mode = 0;
        msg->stat.size = 0;
        msg->stat.time = 0;
    }

    svc->state = AFS_STATE_PROCESS_STAT;
    p->write_len = sizeof(msg->stat);
    return 1;
}

static int state_init_list(afs_service_t *svc, apacket *p)
{
    union syncmsg *msg = (union syncmsg*)p->data;

    int len = strlen(svc->buff);
    /* CONFIG_PATH_MAX + "/" + 1 char filename at least + "\x0" => -3 */
    if (len >= CONFIG_PATH_MAX-3) {
        adb_log("Dir name too big\n");
        goto exit_done;
    }

    svc->list.path = (char*)malloc(CONFIG_PATH_MAX); // len+1+128);
    if (svc->list.path == NULL) {
        adb_log("Cannot allocate dirname\n");
        goto exit_done;
    }

    memcpy(svc->list.path, svc->buff, len);
    svc->list.path[len] = '/';
    svc->list.file_ptr = svc->list.path+len +1;

    svc->list.d = opendir(svc->buff);
    if(svc->list.d == NULL) {
        goto exit_free;
    }

    svc->state = AFS_STATE_PROCESS_LIST;
    /* Fill first frame */
    return state_process_list(svc, p);

exit_free:
    free(svc->list.path);
exit_done:
    msg->dent.id = ID_DONE;
    msg->dent.mode = 0;
    msg->dent.size = 0;
    msg->dent.time = 0;
    msg->dent.namelen = 0;
    p->write_len = sizeof(msg->dent);
    return 0;
}

static int state_process_list(afs_service_t *svc, apacket *p)
{
    int ret;
    struct dirent *de;
    struct stat st;
    union syncmsg *msg = (union syncmsg*)p->data;

    /* Only send items one by one to ensure there is no overflow in packet */

    de = readdir(svc->list.d);

    if (de == NULL) {
        msg->dent.id = ID_DONE;
        msg->dent.mode = 0;
        msg->dent.size = 0;
        msg->dent.time = 0;
        msg->dent.namelen = 0;
        p->write_len = sizeof(msg->dent);
        return 0;
    }

    int len = strlen(de->d_name);

    int remaining = CONFIG_PATH_MAX -
        (int)(svc->list.file_ptr - svc->list.path);

    if (len >= remaining) {
        adb_log("filename <%s> too long to stat: %d/%d\n",
            de->d_name, len, remaining);
        msg->dent.mode = 0;
        msg->dent.size = 0;
        msg->dent.time = 0;
    }
    else {
        /* Try to stat file */
        memcpy(svc->list.file_ptr, de->d_name, len);
        svc->list.file_ptr[len] = 0;

        /* Do not follow symlinks */
        if((ret = lstat(svc->list.path, &st))) {
            adb_log("stat failed <%s> %d %d\n", svc->list.path, ret, errno);
            st.st_mode = 0;
            st.st_size = 0;
            st.st_mtime = 0;
        }

        adb_log("list file <%s> 0x%x 0x%x s=0x%x t=0x%x\n",
            svc->list.path, st.st_mode, htoll(st.st_mode),
            st.st_size, st.st_mtime);

        msg->dent.mode = htoll(st.st_mode);
        msg->dent.size = htoll(st.st_size);
        msg->dent.time = htoll(st.st_mtime);
    }

    if (len > (int)(CONFIG_ADB_PAYLOAD_SIZE - sizeof(msg->dent))) {
        adb_log("filename <%s> too long: %d/%d\n",
            de->d_name,
            len,
            (int)(CONFIG_ADB_PAYLOAD_SIZE - sizeof(msg->dent)));
        len = CONFIG_ADB_PAYLOAD_SIZE - sizeof(msg->dent);
    }

    msg->dent.id = ID_DENT;
    msg->dent.namelen = htoll(len);

    memcpy((&msg->dent)+1, de->d_name, len);
    p->write_len = sizeof(msg->dent) + len;
    return 1;
}

static int state_init_send(afs_service_t *svc, apacket *p)
{
    unsigned int mode;
    bool is_link;

    char* tmp = strrchr(svc->buff,',');
    if(tmp) {
        *tmp = 0;
        mode = strtoul(tmp + 1, NULL, 0);
        is_link = S_ISLNK((mode_t) mode);
        mode &= 0777;
    }
    else {
        mode = 0644;
        is_link = 0;
    }

    /* TODO always unlink or stat file ?
     * Useless for regular files (O_CREAT | O_TRUNC)
     * but may be required for symlinks or folders ?
     */
    unlink(svc->buff);

    if(create_path_directories(svc->buff) != 0) {
        prepare_fail_errno(svc, p);
        return 0;
    }

    if (is_link) {
        return state_init_send_link(svc, p);
    }

    return state_init_send_file(svc, p, mode);
}

static int state_init_send_file(afs_service_t *svc, apacket *p, mode_t mode)
{
    svc->send_file.fd = open(svc->buff, O_WRONLY | O_CREAT | O_TRUNC, mode);

    if(svc->send_file.fd < 0) {
        adb_log("failed to open file <%s> (fd=%d)\n", svc->buff, svc->send_file.fd);
        prepare_fail_errno(svc, p);
        return 0;
    }

    svc->state = AFS_STATE_PROCESS_SEND_FILE_HDR;
    return 1;
}

static int state_init_send_link(afs_service_t *svc, apacket *p)
{
    int len;

    len = strlen(svc->buff);
    if (len >= CONFIG_PATH_MAX) {
        prepare_fail_message(svc, p, "path too long");
        return 0;
    }
    svc->send_link.path = (char*)malloc(len+1);
    if (svc->send_link.path == NULL) {
        adb_log("Cannot allocate dirname\n");
        prepare_fail_message(svc, p, "out of memory");
        return 0;
    }

    memcpy(svc->send_link.path, svc->buff, len);
    svc->send_link.path[len] = 0;
    svc->state = AFS_STATE_PROCESS_SEND_SYM_HDR;
    return 1;
}

static int state_process_send_header(afs_service_t *svc, apacket *p)
{
    int ret;
    union syncmsg *msg = (union syncmsg*)svc->buff;

    ret = read_from_packet(svc, p, sizeof(msg->data));
    if (ret != 0) {
        if (ret == -EAGAIN) {
            return 1;
        }
        prepare_fail_message(svc, p, "read error");
        return 0;
    }

    if(msg->data.id != ID_DATA) {
        if(msg->data.id == ID_DONE) {
            prepare_okay_message(svc, p);
            return 1;
        }
        adb_log("Data message is 0x%x\n", msg->data.id);
        prepare_fail_message(svc, p, "invalid data message");
        return 0;
    }
    svc->namelen = ltohl(msg->data.size);
    if(svc->namelen >= SYNC_TEMP_BUFF_SIZE) {
        prepare_fail_message(svc, p, "oversize data message");
        return 0;
    }

    svc->state += 1;
    return 1;
}

static int state_process_send_file(afs_service_t *svc, apacket *p)
{
    int ret;

    while (svc->namelen > 0) {
        int block_size = min(SYNC_TEMP_BUFF_SIZE, svc->namelen);

        ret = read_from_packet(svc, p, block_size);
        if (ret != 0) {
            if (ret == -EAGAIN) {
                return 1;
            }
            prepare_fail_message(svc, p, "read error");
            return 0;
        }

        svc->namelen -= block_size;

        if (svc->send_file.fd >= 0) {

            /* Write data to file */
            char *write_ptr = svc->buff;
            while (block_size > 0) {
                ret = write(svc->send_file.fd, write_ptr, block_size);
                if (ret > 0) {
                    write_ptr += ret;
                    block_size -= ret;
                    continue;
                }
                if (ret < 0 && errno == EINTR) {
                    continue;
                }
                /* TODO handle nonblocking io */
                adb_log("write error %d %d\n", ret, errno);
                prepare_fail_message(svc, p, "write error");
                return 0;
            }
        }
    }

    /* Wait for DONE frame */
    svc->state -= 1;
    return 1;
}

#ifdef CONFIG_SYSTEM_ADB_FILE_SYMLINK
static int state_process_send_sym(afs_service_t *svc, apacket *p) {
    int ret;

    ret = read_from_packet(svc, p, svc->namelen);
    if (ret != 0) {
        if (ret == -EAGAIN) {
            return 1;
        }
        prepare_fail_message(svc, p, "read error");
        return 0;
    }

    if (svc->namelen >= SYNC_TEMP_BUFF_SIZE) {
        prepare_fail_message(svc, p, "symlink target too long");
        return 0;
    }

    svc->buff[svc->namelen] = 0;
    ret = symlink(svc->buff, svc->send_link.path);

    if (ret) {
        adb_log("symlink failed %d %d\n", ret, errno);
        prepare_fail_message(svc, p, "symlink call failed");
        return 0;
    }

    /* Wait for DONE frame */
    svc->state -= 1;
    return 1;
}
#endif

static int state_init_recv(afs_service_t *svc, apacket *p)
{
    svc->recv.fd = open(svc->buff, O_RDONLY);
    if(svc->recv.fd < 0) {
        adb_log("Cannot open file for read %d\n", svc->recv.fd);
        prepare_fail_message(svc, p, "file does not exist");
        return 0;
    }

    /* FIXME handle non blocking io ? */
    // flags = fcntl(svc->recv.fd, F_GETFL, 0);
    // fcntl(svc->recv.fd, F_SETFL, flags | O_NONBLOCK);

    svc->state = AFS_STATE_PROCESS_RECV;
    return state_process_recv(svc, p);
}

static int state_process_recv(afs_service_t *svc, apacket *p)
{
    int ret;
    union syncmsg *msg = (union syncmsg*)p->data;

    ret = read(svc->recv.fd,
        (&msg->data)+1,
        CONFIG_ADB_PAYLOAD_SIZE-sizeof(msg->data));

    if (ret > 0) {
        msg->data.id = ID_DATA;
        msg->data.size = htoll(ret);
        p->write_len = sizeof(msg->data) + ret;
        return 1;
    }

    if (ret == 0) {
        msg->status.id = ID_DONE;
        msg->status.msglen = 0;
        p->write_len = sizeof(msg->status);
        return 0;
    }

    /* TODO handle non blocking and EAGAIN */

    adb_log("read failed %d %d\n", ret, errno);
    prepare_fail_message(svc, p, "read failed");
    return 0;
}

int state_wait_cmd(afs_service_t *svc, apacket *p)
{
    int ret;
    union syncmsg *msg;

    ret = read_from_packet(svc, p, sizeof(msg->req));
    if (ret != 0) {
        if (ret == -EAGAIN) {
            return 1;
        }
        return -1;
    }

    msg = (union syncmsg*)svc->buff;

    if (msg->req.id == ID_QUIT) {
        /* Return error code so service gets closed */
        return -1; // OK; // ERROR;
    }

    if (msg->req.namelen >= CONFIG_PATH_MAX) {
        adb_log("Fail path too big (%d)\n", msg->req.namelen);
        return -1;
    }

    svc->cmd = msg->req.id;
    svc->namelen = msg->req.namelen;

    svc->state = AFS_STATE_WAIT_CMD_DATA;
    return 1;
}

int state_wait_cmd_data(afs_service_t *svc, apacket *p)
{
    int ret;

    ret = read_from_packet(svc, p, svc->namelen);
    if (ret != 0) {
        if (ret == -EAGAIN) {
            return 1;
        }
        return -1;
    }

    if (svc->namelen >= SYNC_TEMP_BUFF_SIZE) {
        return -1;
    }

    svc->buff[svc->namelen] = 0;

    switch(svc->cmd) {
    case ID_STAT:
        ret = state_init_stat(svc, p);
        break;
    case ID_LIST:
        ret = state_init_list(svc, p);
        break;
    case ID_SEND:
        ret = state_init_send(svc, p);
        break;
    case ID_RECV:
        ret = state_init_recv(svc, p);
        break;

    case ID_QUIT:
        // adb_log("got QUIT command\n");
        ret = 0;
        break;

    default:
        adb_log("Unexpected command 0x%x\n", svc->cmd);
        ret = -1;
    }

    return ret;
}

static int file_sync_on_write(adb_service_t *service, apacket *p) {
    int ret;
    afs_service_t *svc = container_of(service, afs_service_t, service);
    svc->packet_ptr = p->data;

    /* Process all packet data */

    while (p->msg.data_length > 0) {
        switch(svc->state) {
            case AFS_STATE_WAIT_CMD:
                ret = state_wait_cmd(svc, p);
                break;
            case AFS_STATE_WAIT_CMD_DATA:
                ret = state_wait_cmd_data(svc, p);
                break;

            case AFS_STATE_PROCESS_SEND_FILE_HDR:
            case AFS_STATE_PROCESS_SEND_SYM_HDR:
                ret = state_process_send_header(svc, p);
                break;

            case AFS_STATE_PROCESS_SEND_FILE_DATA:
                ret = state_process_send_file(svc, p);
                break;

            case AFS_STATE_PROCESS_SEND_SYM_DATA:
#ifdef CONFIG_SYSTEM_ADB_FILE_SYMLINK
                ret = state_process_send_sym(svc, p);
#else
                adb_log("symlink not supported\n");
                ret = -1;
#endif
                break;

            default:
                adb_log("Unexpected state %d\n", svc->state);
                ret = -1;
        }

        if (ret > 0) {
            continue;
        }

        /* process done or error, reset state */
        state_reset(svc);
        return ret;
    }

    /* process ok, wait for next frame */
    return 0;
}

static int file_sync_on_ack(adb_service_t *service, apacket *p) {
    int ret;
    afs_service_t *svc = container_of(service, afs_service_t, service);
    svc->packet_ptr = p->data;

    /* No data in notify packet */
    switch (svc->state) {
        case AFS_STATE_WAIT_CMD:
            // adb_log("STATE IDLE/QUIT\n");
            ret = -1;
            break;

        case AFS_STATE_PROCESS_RECV:
            ret = state_process_recv(svc, p);
            break;

        case AFS_STATE_PROCESS_LIST:
            ret = state_process_list(svc, p);
            break;

        case AFS_STATE_PROCESS_STAT:
        case AFS_STATE_PROCESS_SEND_FILE_HDR:
        case AFS_STATE_PROCESS_SEND_SYM_HDR:
            ret = 0;
            break;

        default:
            adb_log("ERROR state %d\n", svc->state);
            ret = -1;
            break;
    }

    if (ret > 0) {
        /* process ok, continue with next frame */
        return 0;
    }

    /* process done or error, reset state */
    state_reset(svc);
    return ret;
}

static void file_sync_close(struct adb_service_s *service) {
    afs_service_t *svc = container_of(service, afs_service_t, service);
    state_reset(svc);
    free(svc);
}

static const adb_service_ops_t file_sync_ops = {
    .on_write_frame = file_sync_on_write,
    .on_ack_frame   = file_sync_on_ack,
    .on_kick        = NULL,
    .close          = file_sync_close
};

adb_service_t* file_sync_service(const char *params)
{
    UNUSED(params);
    afs_service_t *service =
        (afs_service_t*)malloc(sizeof(afs_service_t));

    if (service == NULL) {
        return NULL;
    }

    service->size = 0;
    service->state = AFS_STATE_WAIT_CMD;
    service->service.ops = &file_sync_ops;

    return &service->service;
}

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

#define htoll(x) (x)
#define ltohl(x) (x)

#define MKID(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))

#define ID_STAT MKID('S','T','A','T')
#define ID_LIST MKID('L','I','S','T')
#define ID_ULNK MKID('U','L','N','K')
#define ID_SEND MKID('S','E','N','D')
#define ID_RECV MKID('R','E','C','V')
#define ID_DENT MKID('D','E','N','T')
#define ID_DONE MKID('D','O','N','E')
#define ID_DATA MKID('D','A','T','A')
#define ID_OKAY MKID('O','K','A','Y')
#define ID_FAIL MKID('F','A','I','L')
#define ID_QUIT MKID('Q','U','I','T')

#define min(a,b) ((a) < (b) ? (a):(b))

#define SYNC_TEMP_BUFF_SIZE PATH_MAX

/****************************************************************************
 * Private types
 ****************************************************************************/

union syncmsg {
    unsigned id;
    struct {
        unsigned id;
        unsigned namelen;
    } req;
    struct {
        unsigned id;
        unsigned mode;
        unsigned size;
        unsigned time;
    } stat;
    struct {
        unsigned id;
        unsigned mode;
        unsigned size;
        unsigned time;
        unsigned namelen;
    } dent;
    struct {
        unsigned id;
        unsigned size;
    } data;
    struct {
        unsigned id;
        unsigned msglen;
    } status;
};

enum {
    AFS_STATE_WAIT_CMD,
    AFS_STATE_WAIT_CMD_DATA,
    AFS_STATE_PROCESS_RECV,
    AFS_STATE_PROCESS_LIST,
    AFS_STATE_PROCESS_SEND_FILE_HDR,
    AFS_STATE_PROCESS_SEND_FILE_DATA,
    AFS_STATE_PROCESS_SEND_SYM_HDR,
    AFS_STATE_PROCESS_SEND_SYM_DATA
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
    char buff[SYNC_TEMP_BUFF_SIZE + 1];
} afs_service_t;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

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
#ifdef CONFIG_ADBD_FILE_SYMLINK
static int state_process_send_sym(afs_service_t *svc, apacket *p);
#endif
static int state_process_recv(afs_service_t *svc, apacket *p);
static int state_process_list(afs_service_t *svc, apacket *p);

/* Reset service state */

static void state_reset(afs_service_t *svc);

/* Frame processing */

static int file_sync_on_ack(adb_service_t *service, apacket *p);
static int file_sync_on_write(adb_service_t *service, apacket *p);
static void file_sync_on_close(struct adb_service_s *service);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void prepare_fail_message(afs_service_t *svc, apacket *p, const char *reason)
{
    UNUSED(svc);
    int len;
    union syncmsg *msg = (union syncmsg*)(p->data + p->write_len);

    adb_err("sync: failure: %s\n", reason);

    len = min(strlen(reason),
        CONFIG_ADBD_PAYLOAD_SIZE - sizeof(msg->data) - p->write_len);
    memcpy((char*)(&msg->data+1), reason, len);

    msg->data.id = ID_FAIL;
    msg->data.size = htoll(len);

    p->write_len += sizeof(msg->data) + len;
}

static void prepare_fail_errno(afs_service_t *svc, apacket *p)
{
    prepare_fail_message(svc, p, strerror(errno));
}

static void prepare_okay_message(afs_service_t *svc, apacket *p)
{
    UNUSED(svc);
    union syncmsg *msg = (union syncmsg*)(p->data + p->write_len);

    msg->status.id = ID_OKAY;
    msg->status.msglen = 0;
    p->write_len += sizeof(msg->status);
}

static int read_from_packet(afs_service_t *svc, apacket *p, unsigned int size)
{
    int ret = -EINVAL;

    if (svc->size > size || size > SYNC_TEMP_BUFF_SIZE) {
        goto exit_reset;
    }

    unsigned int chunk_size = min(size-svc->size, p->msg.data_length);

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

    if(name[0] != '/') return -1;

    for(;;) {
        x = strchr(++x, '/');
        if(!x) return 0;
        *x = 0;

        ret = mkdir(name, mode);
        *x = '/';
        if((ret < 0) && (errno != EEXIST)) {
            adb_err("mkdir <%s> failed: %d\n", name, errno);
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
    union syncmsg *msg = (union syncmsg*)(p->data + p->write_len);

    msg->stat.id = ID_STAT;
    p->write_len += sizeof(msg->stat);

    if(!stat(svc->buff, &st)) {
        msg->stat.mode = htoll(st.st_mode);
        msg->stat.size = htoll(st.st_size);
        msg->stat.time = htoll(st.st_mtime);

        /* There may be more data to process in current frame */
        svc->state = AFS_STATE_WAIT_CMD;
        return 1;
    }

    /* File not found */
    msg->stat.mode = 0;
    msg->stat.size = 0;
    msg->stat.time = 0;
    return 0;
}

static int state_init_list(afs_service_t *svc, apacket *p)
{
    union syncmsg *msg = (union syncmsg*)(p->data + p->write_len);

    int len = strlen(svc->buff);
    /* PATH_MAX + "/" + 1 char filename at least + "\x0" => -3 */
    if (len >= PATH_MAX-3) {
        adb_err("Dir name too big\n");
        goto exit_done;
    }

    svc->list.path = (char*)malloc(PATH_MAX);
    if (svc->list.path == NULL) {
        adb_err("Cannot allocate dirname\n");
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
    p->write_len += sizeof(msg->dent);
    return 0;
}

static int state_process_list(afs_service_t *svc, apacket *p)
{
    int ret;
    struct dirent *de;
    struct stat st;
    union syncmsg *msg = (union syncmsg*)(p->data + p->write_len);

    /* Only send items one by one to ensure there is no overflow in packet */

    de = readdir(svc->list.d);

    if (de == NULL) {
        msg->dent.id = ID_DONE;
        msg->dent.mode = 0;
        msg->dent.size = 0;
        msg->dent.time = 0;
        msg->dent.namelen = 0;
        p->write_len += sizeof(msg->dent);
        return 0;
    }

    int len = strlen(de->d_name);

    int remaining = PATH_MAX -
        (int)(svc->list.file_ptr - svc->list.path);

    if (len >= remaining) {
        adb_err("filename <%s> too long to stat: %d/%d\n",
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
            adb_err("stat failed <%s> %d %d\n", svc->list.path, ret, errno);
            st.st_mode = 0;
            st.st_size = 0;
            st.st_mtime = 0;
        }

        msg->dent.mode = htoll(st.st_mode);
        msg->dent.size = htoll(st.st_size);
        msg->dent.time = htoll(st.st_mtime);
    }

    remaining = (int)(CONFIG_ADBD_PAYLOAD_SIZE - sizeof(msg->dent) - p->write_len);
    if (len > remaining) {
        adb_err("filename <%s> too long: %d/%d\n",
            de->d_name,
            len,
            remaining);
        len = remaining;
    }

    msg->dent.id = ID_DENT;
    msg->dent.namelen = htoll(len);

    memcpy((&msg->dent)+1, de->d_name, len);
    p->write_len += sizeof(msg->dent) + len;
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
        is_link = S_ISLNK((mode_t)mode);
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
        adb_err("failed to create path <%s>\n", svc->buff);
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
        adb_err("failed to open file <%s> (fd=%d)\n",
            svc->buff, svc->send_file.fd);
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
    if (len >= PATH_MAX) {
        prepare_fail_message(svc, p, "path too long");
        return 0;
    }
    svc->send_link.path = (char*)malloc(len+1);
    if (svc->send_link.path == NULL) {
        adb_err("Cannot allocate dirname\n");
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
            return 0;
        }
        adb_log("Data message is 0x%x\n", msg->data.id);
        prepare_fail_message(svc, p, "invalid data message");
        return 0;
    }

    /* Read file length */
    svc->namelen = ltohl(msg->data.size);

    svc->state += 1;
    return 1;
}

static int state_process_send_file(afs_service_t *svc, apacket *p)
{
    int ret;
    int block_size = min(p->msg.data_length, svc->namelen);
    uint8_t *write_ptr = svc->packet_ptr;

    svc->namelen -= block_size;
    p->msg.data_length -= block_size;
    svc->packet_ptr += block_size;

    if (svc->send_file.fd >= 0) {

        /* Write data to file */
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
            adb_err("write error %d %d\n", ret, errno);
            prepare_fail_message(svc, p, "write error");
            return 0;
        }
    }

    /* Wait for DONE frame */
    if (svc->namelen == 0)
        svc->state -= 1;

    return 1;
}

#ifdef CONFIG_ADBD_FILE_SYMLINK
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

    svc->buff[svc->namelen] = 0;
    ret = symlink(svc->buff, svc->send_link.path);

    if (ret) {
        adb_err("symlink failed %d %d\n", ret, errno);
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
        adb_err("Cannot open file <%s> for read %d\n",
            svc->buff, errno);
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
    union syncmsg *msg = (union syncmsg*)(p->data + p->write_len);

    ret = read(svc->recv.fd,
        (&msg->data)+1,
        CONFIG_ADBD_PAYLOAD_SIZE - sizeof(msg->data) - p->write_len);

    if (ret > 0) {
        msg->data.id = ID_DATA;
        msg->data.size = htoll(ret);
        p->write_len += sizeof(msg->data) + ret;
        return 1;
    }

    if (ret == 0) {
        msg->status.id = ID_DONE;
        msg->status.msglen = 0;
        p->write_len += sizeof(msg->status);
        return 0;
    }

    /* TODO handle non blocking and EAGAIN */

    adb_err("read failed %d %d\n", ret, errno);
    prepare_fail_message(svc, p, "read failed");
    return 0;
}

static int state_wait_cmd(afs_service_t *svc, apacket *p)
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
        return -1;
    }

    if (msg->req.namelen >= PATH_MAX) {
        adb_err("Fail path too big (%d)\n", msg->req.namelen);
        return -1;
    }

    svc->cmd = msg->req.id;
    svc->namelen = msg->req.namelen;

    svc->state = AFS_STATE_WAIT_CMD_DATA;
    return 1;
}

static int state_wait_cmd_data(afs_service_t *svc, apacket *p)
{
    int ret;

    ret = read_from_packet(svc, p, svc->namelen);
    if (ret != 0) {
        if (ret == -EAGAIN) {
            return 1;
        }
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

    default:
        adb_err("Unexpected command 0x%x\n", svc->cmd);
        ret = -1;
    }

    return ret;
}

static int file_sync_on_write(adb_service_t *service, apacket *p) {
    int ret = 0;
    afs_service_t *svc = container_of(service, afs_service_t, service);
    svc->packet_ptr = p->data;

    /* Process all packet data */

    while (p->msg.data_length > 0 && ret >= 0) {
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
#ifdef CONFIG_ADBD_FILE_SYMLINK
                ret = state_process_send_sym(svc, p);
#else
                adb_err("symlink not supported\n");
                ret = -1;
#endif
                break;

            default:
                adb_err("Unexpected state %d\n", svc->state);
                ret = -1;
        }

        /* process done or error, reset state */
        if (ret <= 0) {
            state_reset(svc);
        }
    }

    /* process ok, wait for next frame */
    return ret >= 0 ? 0 : ret;
}

static int file_sync_on_ack(adb_service_t *service, apacket *p) {
    int ret;
    afs_service_t *svc = container_of(service, afs_service_t, service);
    svc->packet_ptr = p->data;

    /* No data in notify packet */
    switch (svc->state) {
        case AFS_STATE_PROCESS_RECV:
            ret = state_process_recv(svc, p);
            break;

        case AFS_STATE_PROCESS_LIST:
            ret = state_process_list(svc, p);
            break;

        case AFS_STATE_PROCESS_SEND_FILE_HDR:
        case AFS_STATE_PROCESS_SEND_SYM_HDR:
        case AFS_STATE_WAIT_CMD:
            /* Nothing to do */
            ret = 0;
            break;

        default:
            adb_err("ERROR state %d\n", svc->state);
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

static void file_sync_on_close(struct adb_service_s *service) {
    afs_service_t *svc = container_of(service, afs_service_t, service);
    state_reset(svc);
    free(svc);
}

static const adb_service_ops_t file_sync_ops = {
    .on_write_frame = file_sync_on_write,
    .on_ack_frame   = file_sync_on_ack,
    .on_kick        = NULL,
    .on_close       = file_sync_on_close
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

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

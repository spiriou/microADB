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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "adb.h"
#include "file_sync_service.h"
#ifdef CONFIG_ADBD_LOGCAT_SERVICE
#include "logcat_service.h"
#endif
#ifdef CONFIG_ADBD_SHELL_SERVICE
#include "shell_service.h"
#endif
#ifdef CONFIG_ADBD_SOCKET_SERVICE
#include "tcp_service.h"
#endif

#define REBOOT_SERVICE ((adb_service_t *)(~0ul))

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void send_frame(adb_client_t *s, apacket *p);
static void send_close_frame(adb_client_t *s, apacket *p,
    unsigned local, unsigned remote);
static void send_cnxn_frame(adb_client_t *s, apacket *p);

#ifdef CONFIG_ADBD_AUTHENTICATION
static void send_auth_request(adb_client_t *client, apacket *p);
static void handle_auth_frame(adb_client_t *client, apacket *p);
#endif

static void handle_open_frame(adb_client_t *client, apacket *p);
static void handle_close_frame(adb_client_t *client, apacket *p);
static void handle_write_frame(adb_client_t *client, apacket *p);
static void handle_okay_frame(adb_client_t *client, apacket *p);

static adb_service_t *adb_service_open(adb_client_t *client,
                                       const char *name, apacket *p);

static adb_service_t* adb_client_find_service(adb_client_t *client,
                                              int id, int peer_id);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void send_frame(adb_client_t *client, apacket *p)
{
    unsigned char *x;
    unsigned int sum;
    unsigned int count;

    p->msg.magic = p->msg.command ^ 0xffffffff;

    count = p->msg.data_length;
    x = (unsigned char *)p->data;
    sum = 0;
    while(count-- > 0){
        sum += *x++;
    }
    p->msg.data_check = sum;

    int ret = client->ops->write(client, p);

    if (ret != 0) {
        adb_err("write failed %d %d\n",
            ret, sizeof(p->msg)+p->msg.data_length);
        client->ops->close(client);
    }
}

static void send_close_frame(adb_client_t *client, apacket *p,
    unsigned local, unsigned remote)
{
    p->msg.command = A_CLSE;
    p->msg.arg0 = local;
    p->msg.arg1 = remote;
    p->msg.data_length = 0;
    p->write_len = 0;
    send_frame(client, p);
}

static void send_cnxn_frame(adb_client_t *client, apacket *p)
{
    p->msg.command = A_CNXN;
    p->msg.arg0 = A_VERSION;
    p->msg.arg1 = CONFIG_ADBD_PAYLOAD_SIZE;
    p->msg.data_length = adb_fill_connect_data((char *)p->data,
                                               CONFIG_ADBD_CNXN_PAYLOAD_SIZE);
    send_frame(client, p);
}

#ifdef CONFIG_ADBD_AUTHENTICATION
static void send_auth_request(adb_client_t *client, apacket *p)
{
    int ret;

    /* Generate random token */

    ret = adb_hal_random(client->token, sizeof(client->token));

    if (ret < 0) {
        adb_err("Failed to generate auth token %d %d\n", ret, errno);
        adb_hal_apacket_release(client, p);
        client->ops->close(client);
        return;
    }

    memcpy(p->data, client->token, sizeof(client->token));
    p->msg.command = A_AUTH;
    p->msg.arg0 = ADB_AUTH_TOKEN;
    p->msg.arg1 = 0;
    p->msg.data_length = sizeof(client->token);
    send_frame(client, p);
}
#endif /* CONFIG_ADBD_AUTHENTICATION */

static void handle_open_frame(adb_client_t *client, apacket *p) {
    adb_service_t *svc;
    char *name = (char*)p->data;

    /* OPEN(local-id, 0, "destination") */
    if (p->msg.arg0 == 0 || p->msg.arg1 != 0) {
        adb_hal_apacket_release(client, p);
        return;
    }

    name[p->msg.data_length > 0 ? p->msg.data_length - 1 : 0] = 0;
    svc = adb_service_open(client, name, p);
    if(svc == NULL) {
        if (p->write_len > 0) {
            /* One shot service returned data */
            adb_send_okay_frame_with_data(client, p, client->next_service_id++,
                                          p->msg.arg0);
        }
        else {
            send_close_frame(client, p, 0, p->msg.arg0);
        }
    } else if (svc != REBOOT_SERVICE) {
        if (p->write_len == APACKET_SERVICE_INIT_ASYNC) {
            /* Service init is asynchronous. Release apacket. */
            adb_hal_apacket_release(client, p);
        }
        else {
            adb_send_okay_frame_with_data(client, p, svc->id, svc->peer_id);
        }
    }
}

static void handle_close_frame(adb_client_t *client, apacket *p) {
    /* CLOSE(local-id, remote-id, "") or CLOSE(0, remote-id, "") */
    adb_service_t *svc;

    svc = adb_client_find_service(client, p->msg.arg1, p->msg.arg0);
    if (svc != NULL) {
       adb_service_close(client, svc, NULL);
    }
    adb_hal_apacket_release(client, p);
}

static void handle_write_frame(adb_client_t *client, apacket *p) {
    /* WRITE(local-id, remote-id, <data>) */
    int ret;
    adb_service_t *svc;
    svc = adb_client_find_service(client, p->msg.arg1, p->msg.arg0);
    if (svc == NULL) {
        /* Ensure service is closed on peer side */
        send_close_frame(client, p, p->msg.arg1, p->msg.arg0);
        return;
    }

    ret = svc->ops->on_write_frame(svc, p);
    if (ret < 0) {
        /* An error occured, stop service */
        adb_service_close(client, svc, p);
        return;
    }

    if (ret == 0) {
        /* Write frame processing done, send acknowledge frame */
        adb_send_okay_frame_with_data(client, p, svc->id, svc->peer_id);
        return;
    }

    /* Service process frame asynchronously, do nothing */
}

static void handle_okay_frame(adb_client_t *client, apacket *p) {
    /* READY(local-id, remote-id, "") */
    int ret;
    adb_service_t *svc;
    svc = adb_client_find_service(client, p->msg.arg1, 0);
    if (!svc) {
        send_close_frame(client, p, p->msg.arg1, p->msg.arg0);
        return;
    }

    if (svc->peer_id == 0) {
        /* Update peer id */
        svc->peer_id = p->msg.arg0;
    }

    ret = svc->ops->on_ack_frame(svc, p);
    if (ret < 0) {
        /* An error occured, stop service */
        adb_service_close(client, svc, p);
        return;
    }

    if (ret > 0) {
        /* Service process frame asynchronously, do nothing */
        return;
    }

    if (p->write_len > 0) {
        /* Write payload from service */
        p->msg.arg0 = svc->id;
        p->msg.arg1 = svc->peer_id;
        adb_send_data_frame(client, p);
        return;
    }

    adb_hal_apacket_release(client, p);
}

#ifdef CONFIG_ADBD_AUTHENTICATION
static void handle_auth_frame(adb_client_t *client, apacket *p) {
    /* AUTH(type, 0, "data") */
    switch (p->msg.arg0) {
        case ADB_AUTH_TOKEN:
            adb_hal_apacket_release(client, p);
            break;

        case ADB_AUTH_SIGNATURE:
            /* Check all public keys */
            for (int i=0; g_adb_public_keys[i]; i++) {

                /* FIXME RSA primitives not integrated yet */

                adb_log("skip key verification\n");
#if 0
                if (RSA_verify((RSAPublicKey*)g_adb_public_keys[i],
                    p->data,
                    p->msg.data_length,
                    client->token, SHA_DIGEST_SIZE))
                {
                    /* Key verification successful */
                    goto exit_connected;
                }
#endif
            }

            /* Key verification failed.
             * TODO add retry counter and timer/delay
             */

            send_auth_request(client, p);
            break;

#ifdef CONFIG_ADBD_AUTH_PUBKEY
        case ADB_AUTH_RSAPUBLICKEY:
            adb_log("accept key from%s\n", strstr((char*)p->data, " "));
            /* FIXME always accept all new public keys for now */
            goto exit_connected;
#endif

        default:
            adb_err("invalid id %d\n", p->msg.arg0);
            adb_hal_apacket_release(client, p);
    }

    return;

exit_connected:
    send_cnxn_frame(client, p);
    client->is_connected = 1;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void adb_send_okay_frame(adb_client_t *client, apacket *p,
    unsigned local, unsigned remote)
{
    p->write_len = 0;
    return adb_send_okay_frame_with_data(client, p, local, remote);
}

void adb_send_okay_frame_with_data(adb_client_t *client, apacket *p,
    unsigned local, unsigned remote)
{
    p->msg.command = A_OKAY;
    p->msg.arg0 = local;
    p->msg.arg1 = remote;
    p->msg.data_length = 0;
    send_frame(client, p);
}

void adb_send_open_frame(adb_client_t *client, apacket *p,
    unsigned local, unsigned remote, int size)
{
    p->msg.command = A_OPEN;
    p->msg.arg0 = local;
    p->msg.arg1 = remote;
    p->msg.data_length = size;
    send_frame(client, p);
}

void adb_send_data_frame(adb_client_t *client, apacket *p)
{
    p->msg.command = A_WRTE;
    p->msg.data_length = p->write_len;
    p->write_len = 0;
    send_frame(client, p);
}

void adb_register_service(adb_service_t *svc, adb_client_t *client) {
    svc->id = client->next_service_id++;
    svc->next = client->services;
    adb_log("id=%d, peer=%d\n", svc->id, svc->peer_id);
    client->services = svc;
}

static adb_service_t *adb_service_open(adb_client_t *client, const char *name, apacket *p)
{
    adb_service_t *svc = NULL;

    if (client->next_service_id == 0) {
        /* service id overflow, exit */
        fatal("service_id overflow");
    }

    do {
#ifdef CONFIG_ADBD_FILE_SERVICE
        if(!strncmp(name, "sync:", 5)) {
            svc = file_sync_service(name);
            break;
        }
#endif

#ifdef CONFIG_ADBD_SOCKET_SERVICE
        if (!strncmp(name, "tcp:", 4)) {
            svc = tcp_forward_service(client, name, p);
            break;
        }
#endif

#if defined(CONFIG_ADBD_SHELL_SERVICE) || defined(CONFIG_ADBD_LOGCAT_SERVICE)
        if (!strncmp(name, "shell", 5)) {
#ifdef CONFIG_ADBD_LOGCAT_SERVICE
            /* Search for logcat */
            char *ptr = strstr(name, "exec logcat");
            if (ptr) {
                svc = logcat_service(client, name);
                break;
            }
#endif /* CONFIG_ADBD_LOGCAT_SERVICE */
#ifdef CONFIG_ADBD_SHELL_SERVICE
            svc = shell_service(client, name);
            break;
#endif /* CONFIG_ADBD_SHELL_SERVICE */
        }
#endif

        if (!strncmp(name, "reboot:", 7)) {
            adb_send_okay_frame(client, p, client->next_service_id++, p->msg.arg0);
            adb_reboot_impl(&name[7]);
            return REBOOT_SERVICE;
        }
    } while (0);

    if (svc == NULL) {
        adb_err("fail to init service %s\n", name);
        return NULL;
    }

    svc->peer_id = p->msg.arg0;
    adb_register_service(svc, client);
    return svc;
}

void adb_service_close(adb_client_t *client, adb_service_t *svc, apacket *p) {
    adb_service_t *cur_svc = client->services;

    if (cur_svc == svc) {
        client->services = svc->next;
        goto exit_free_service;
    }

    while (cur_svc != NULL && cur_svc->next != NULL) {
        if (cur_svc->next == svc) {
            cur_svc->next = svc->next;
            goto exit_free_service;
        }
        cur_svc = cur_svc->next;
    }

    adb_warn("service %p not found\n", svc);
    return;

exit_free_service:
    if (p) {
        send_close_frame(client, p, svc->id, svc->peer_id);
    }
    svc->ops->on_close(svc);
}

static adb_service_t* adb_client_find_service(adb_client_t *client, int id, int peer_id) {
    adb_service_t *svc;

    svc = client->services;
    while (svc != NULL) {
        if (svc->id == id && (peer_id == 0 || svc->peer_id == peer_id)) {
            return svc;
        }
        svc = svc->next;
    }

    return NULL;
}

void adb_client_kick_services(adb_client_t *client) {
    adb_service_t *service = client->services;
    while (service != NULL) {
        if (service->ops->on_kick) {
            service->ops->on_kick(service);
        }
        service = service->next;
    }
}

static void adb_init_client(adb_client_t *client) {
    /* setup adb_client */
    client->next_service_id = 1;
    client->services = NULL;
    client->is_connected = 0;
}

adb_client_t *adb_create_client(size_t size) {
    adb_client_t *client = adb_hal_create_client(size);
    if (client == NULL) {
        return NULL;
    }

    adb_init_client(client);
    return client;
}

void adb_destroy_client(adb_client_t *client) {
    adb_service_t *service = client->services;
    adb_service_t *next;
    while (service != NULL) {
        adb_log("stop service %d <-> %d\n", service->id, service->peer_id);
        // FIXME send close frame ?
        next = service->next;
        adb_service_close(client, service, NULL);
        service = next;
    }
    adb_hal_destroy_client(client);
}

void adb_process_packet(adb_client_t *client, apacket *p)
{
    p->write_len = 0;

    switch(p->msg.command) {
    case A_CNXN:
        /* CONNECT(version, maxdata, "system-id-string") */
#ifdef CONFIG_ADBD_AUTHENTICATION
        if (!client->is_connected) {
            send_auth_request(client, p);
            return;
        }
#endif /* CONFIG_ADBD_AUTHENTICATION */
        send_cnxn_frame(client, p);
        client->is_connected = 1;
        return;

#ifdef CONFIG_ADBD_AUTHENTICATION
    case A_AUTH:
        if (!client->is_connected) {
            handle_auth_frame(client, p);
            return;
        }
        break;
#endif /* CONFIG_ADBD_AUTHENTICATION */

    case A_OPEN:
        handle_open_frame(client, p);
        return;

    case A_CLSE:
        handle_close_frame(client, p);
        return;

    case A_WRTE:
        handle_write_frame(client, p);
        return;

    case A_OKAY:
        handle_okay_frame(client, p);
        return;

    default:
        break;
    }

    adb_log("handle_packet: what is %08x?!\n", p->msg.command);
    adb_hal_apacket_release(client, p);
    client->ops->close(client);
}

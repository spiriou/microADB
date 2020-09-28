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
#include <string.h>
#include <errno.h>

#include "adb.h"
#include "file_sync_service.h"
#include "tcp_service.h"
#ifdef CONFIG_SYSTEM_ADB_SHELL_SERVICE
#include "shell_service.h"
#endif
#ifdef CONFIG_SYSTEM_ADB_LOGCAT_SERVICE
#include "logcat_service.h"
#endif

#ifdef CONFIG_SYSTEM_ADB_AUTHENTICATION
#include "mincrypt/rsa.h"
#include "mincrypt/sha.h"
#endif

static void send_frame(adb_client_t *s, apacket *p);
static void send_close_frame(adb_client_t *s, apacket *p,
    unsigned local, unsigned remote);
static void send_cnxn_frame(adb_client_t *s, apacket *p);

#ifdef CONFIG_SYSTEM_ADB_AUTHENTICATION
static void send_auth_request(adb_client_t *client, apacket *p);
static void handle_auth_frame(adb_client_t *client, apacket *p);
#endif

static void handle_open_frame(adb_client_t *client, apacket *p);
static void handle_close_frame(adb_client_t *client, apacket *p);
static void handle_write_frame(adb_client_t *client, apacket *p);
static void handle_okay_frame(adb_client_t *client, apacket *p);

static adb_service_t *adb_service_open(adb_client_t *client, const char *name, apacket *p);

static adb_service_t* adb_client_find_service(adb_client_t *client, int id, int peer_id);

static void send_frame(adb_client_t *client, apacket *p)
{
    unsigned char *x;
    unsigned int sum;
    unsigned int count;

    // adb_log("%p %d %.4s (%d -> %d)\n",
    // 	p, p->msg.data_length,
    // 	&p->msg.command,
    //     p->msg.arg0,
    //     p->msg.arg1);

    p->msg.magic = p->msg.command ^ 0xffffffff;

    count = p->msg.data_length;
    x = (unsigned char *) p->data;
    sum = 0;
    while(count-- > 0){
        sum += *x++;
    }
    p->msg.data_check = sum;

    adb_log("WRITE FRAME %p\n", p);
    DumpHex(&p->msg, sizeof(p->msg)+p->msg.data_length);

    int ret = client->ops->write(client, p);

    if (ret != 0) {
        adb_log("write failed %d %d\n",
        	ret, sizeof(p->msg)+p->msg.data_length);
        client->ops->close(client);
    }
}

// static
void send_okay_frame(adb_client_t *client, apacket *p,
	unsigned local, unsigned remote)
{
    p->msg.command = A_OKAY;
    p->msg.arg0 = local;
    p->msg.arg1 = remote;
    p->msg.data_length = 0;
    send_frame(client, p);
}

// static
void send_open_frame(adb_client_t *client, apacket *p,
	unsigned local, unsigned remote, int size)
{
    p->msg.command = A_OPEN;
    p->msg.arg0 = local;
    p->msg.arg1 = remote;
    p->msg.data_length = size;
    send_frame(client, p);
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
    p->msg.arg1 = CONFIG_ADB_PAYLOAD_SIZE;
    p->msg.data_length = adb_fill_connect_data((char *)p->data,
                                               CONFIG_ADB_CNXN_PAYLOAD_SIZE);
    send_frame(client, p);
}

#ifdef CONFIG_SYSTEM_ADB_AUTHENTICATION
static void send_auth_request(adb_client_t *client, apacket *p)
{
    adb_log("Calling send_auth_request\n");
    int ret;

    ret = adb_hal_random(client->token, sizeof(client->token));

    DumpHex(client->token, sizeof(client->token));

    if (ret < 0) {
    	adb_log("Failed to generate auth token %d %d\n", ret, errno);
    	adb_hal_apacket_release(client, p);
    	adb_destroy_client(client);
    	return;
    }

    memcpy(p->data, client->token, sizeof(client->token));
    p->msg.command = A_AUTH;
    p->msg.arg0 = ADB_AUTH_TOKEN;
    p->msg.data_length = sizeof(client->token);
    send_frame(client, p);
}
#endif /* CONFIG_SYSTEM_ADB_AUTHENTICATION */

void adb_client_send_service_payload(adb_client_t *client, apacket *p)
{
    p->msg.command = A_WRTE;
    p->msg.data_length = p->write_len;
    p->write_len = 0;
    send_frame(client, p);
}

static void handle_open_frame(adb_client_t *client, apacket *p) {
    adb_service_t *svc;
    char *name = (char*) p->data;

    /* OPEN(local-id, 0, "destination") */
    if (p->msg.arg0 == 0 || p->msg.arg1 != 0) {
    	return;
    }

    name[p->msg.data_length > 0 ? p->msg.data_length - 1 : 0] = 0;
    svc = adb_service_open(client, name, p);
    if(svc == NULL) {
        if (p->msg.arg1 != 0) {
            /* One shot service returned data */
            adb_log("one shot service %d returned %d\n", p->msg.arg1, p->write_len);
            send_okay_frame(client, p, p->msg.arg1, p->msg.arg0);
        }
        else {
            send_close_frame(client, p, 0, p->msg.arg0);
        }
    } else {
        svc->peer_id = p->msg.arg0;
        send_okay_frame(client, p, svc->id, svc->peer_id);
    }
}

static void handle_close_frame(adb_client_t *client, apacket *p) {
    /* CLOSE(local-id, remote-id, "") or CLOSE(0, remote-id, "") */
    adb_service_t *svc;

    svc = adb_client_find_service(client, p->msg.arg1, p->msg.arg0);
    if (svc != NULL) {
        // send_close_frame(client, p, p->msg.arg1, p->msg.arg0);
        svc->peer_id = p->msg.arg0;
        adb_service_close(client, svc, p);
    }
    else {
        adb_log("GARBAGE CLOSE\n");
        adb_hal_apacket_release(client, p);
    }
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
        // send_close_frame(client, p, p->msg.arg1, p->msg.arg0);
        adb_service_close(client, svc, p);
        return;
    }

    if (ret == 0) {
    	/* Write frame processing done, send acknowledge frame */
    	send_okay_frame(client, p, svc->id, svc->peer_id);
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
	    // send_close_frame(client, p, svc->id, svc->peer_id);
	    adb_service_close(client, svc, p);
	    return;
	}

	if (ret > 0) {
		/* Service process frame asynchronously, do nothing */
		return;
		// goto exit_free_packet;
	}

    if (p->write_len > 0) {
        /* Write payload from service */
        p->msg.arg0 = svc->id;
        p->msg.arg1 = svc->peer_id;
        adb_client_send_service_payload(client, p);
        return;
    }

// exit_free_packet:
	adb_hal_apacket_release(client, p);  
}

#ifdef CONFIG_SYSTEM_ADB_AUTHENTICATION
static void handle_auth_frame(adb_client_t *client, apacket *p) {
    /* READY(local-id, remote-id, "") */
    UNUSED(client);
    UNUSED(p);

    switch (p->msg.arg0) {
        case ADB_AUTH_TOKEN:
        	adb_log("ADB_AUTH_TOKEN\n");
        	adb_hal_apacket_release(client, p);
        	break;

        case ADB_AUTH_SIGNATURE:
        	adb_log("ADB_AUTH_SIGNATURE\n");
        	for (int i=0; g_adb_public_keys[i]; i++) {
        		adb_log("CHECK KEY %d\n", i);
	        	if (RSA_verify((RSAPublicKey*)g_adb_public_keys[i],
	        		p->data,
	        		p->msg.data_length,
	        		client->token, SHA_DIGEST_SIZE)) {
	        		printf("CHECK SUCCESSFUL\n");
	            	goto exit_connected;
	            }
	        }

            /* TODO add retry counter and timer/delay */
            printf("FAIL CHECK\n");
            send_auth_request(client, p);
        	break;

#ifdef CONFIG_SYSTEM_ADB_AUTH_PUBKEY
        case ADB_AUTH_RSAPUBLICKEY:
        	adb_log("accept key from%s\n", strstr((char*)p->data, " "));
        	/* FIXME always accept all new public keys for now */
        	goto exit_connected;
#endif

        default:
        	adb_log("invalid id %d\n", p->msg.arg0);
        	adb_hal_apacket_release(client, p);
    }

    return;

exit_connected:
	send_cnxn_frame(client, p);
	client->is_connected = 1;
}
#endif

void adb_register_service(adb_service_t *svc, adb_client_t *client) {
    svc->id = client->next_service_id++;
    svc->next = client->services;
    adb_log("%p %p\n", client, svc);
    client->services = svc;
}

static adb_service_t *adb_service_open(adb_client_t *client, const char *name, apacket *p)
{
    adb_service_t *svc = NULL;

    UNUSED(p);

    if (client->next_service_id == 0) {
    	/* service id overflow, exit */
    	fatal("service_id overflow");
    }

#ifdef CONFIG_SYSTEM_ADB_FILE_SERVICE
    if(!strncmp(name, "sync:", 5)) {
        svc = file_sync_service(name);
    }
#endif

#if defined(CONFIG_SYSTEM_ADB_SHELL_SERVICE) || defined(CONFIG_SYSTEM_ADB_LOGCAT_SERVICE)
    else if (!strncmp(name, "shell", 5)) {
#ifdef CONFIG_SYSTEM_ADB_LOGCAT_SERVICE
        /* Search for logcat */
        char *ptr = strstr(name, "exec logcat");
        if (ptr) {
            adb_log("FOUND LOGCAT <%s>\n", ptr+5);
            svc = logcat_service(client, name);
            goto service_created;
        }

#endif
#ifdef CONFIG_SYSTEM_ADB_SHELL_SERVICE
        svc = shell_service(client, name);
        goto service_created;
#endif
    }
#endif

    else if (!strncmp(name, "reboot:", 7)) {
        adb_log("got reboot <%s>\n", name);
        p->write_len = sprintf((char*)p->data, "reboot")+1;
        p->msg.arg1 = client->next_service_id++;
    }

#ifdef CONFIG_SYSTEM_ADB_SOCKET_SERVICE
    else if (!strncmp(name, "tcp:", 4)) {
        svc = tcp_forward_service(client, name);
    }

    else if (!strncmp(name, "reverse:", 8)) {
		if (!strncmp(name, "list-forward", 12)) {
			int len = snprintf((char*)p->data, CONFIG_ADB_PAYLOAD_SIZE-1,
				"%s %s %s\n",
				"(reverse)", "tcp:1234", "tcp:9812");
			if (len > CONFIG_ADB_PAYLOAD_SIZE-1) {
				len = CONFIG_ADB_PAYLOAD_SIZE-1;
			}
			p->data[len] = 0;
			p->write_len = len+1;
            p->msg.arg1 = client->next_service_id++;
		}
		else {
        	// svc = &tcp_reverse_service(client, name, p)->service;
        	svc = tcp_reverse_service(client, name, p);
        }

	     if (svc == NULL) {
	    	goto exit_error;
	    }

	    // adb_register_rservice(svc, client);
	    adb_register_service(svc, client);
	    return svc;
    }
#endif

service_created:
    if (svc == NULL) {
    	goto exit_error;
    }

    adb_register_service(svc, client);
    return svc;

exit_error:
    adb_log("fail to init service %s\n", name);
    return NULL;
}

void adb_service_close(adb_client_t *client, adb_service_t *svc, apacket *p) {
    adb_service_t *cur_svc = client->services;
    adb_log("entry %p %p %p\n", client, cur_svc, svc);

    if (cur_svc == svc) {
        client->services = svc->next;
        goto exit_free_service;
    }

    while (cur_svc->next) {
        if (cur_svc->next == svc) {
            cur_svc->next = svc->next;
            goto exit_free_service;
        }
    }

    adb_log("FATAL service %p not found\n", svc);
    return;

exit_free_service:
    // free(svc);
	if (p) {
		send_close_frame(client, p, svc->id, svc->peer_id);
	}
    svc->ops->close(svc);
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
	adb_log("entry %p\n", client);
    adb_service_t *service = client->services;
    while (service != NULL) {
        adb_log("TEST %p\n", service);
        adb_log("kick service %d <-> %d\n", service->id, service->peer_id);
        // FIXME send close frame ?
        if (service->ops->on_kick) {
        	service->ops->on_kick(service);
        }
        service = service->next;
    }
}

adb_client_t *adb_create_client(size_t size) {
    adb_client_t *client = adb_hal_create_client(size);
    if (client == NULL) {
        return NULL;
    }

    adb_log("entry %p\n", client);
    /* setup adb_client */
    client->next_service_id = 1;
    client->services = NULL;
    client->is_connected = 0;
    return client;
}

void adb_destroy_client(adb_client_t *client) {
	adb_log("entry %p\n", client);
    adb_service_t *service = client->services;
    while (service != NULL) {
        adb_log("TEST %p\n", service);
        adb_log("stop service %d <-> %d\n", service->id, service->peer_id);
        // FIXME send close frame ?
        adb_service_close(client, service, NULL);
        service = service->next;
    }
	adb_hal_destroy_client(client);
}

void adb_process_packet(adb_client_t *client, apacket *p)
{
    p->write_len = 0;
    adb_log("READ FRAME %p\n", p);
    DumpHex(&p->msg, sizeof(p->msg)+p->msg.data_length);

    if (!client->is_connected) {
    	if (p->msg.command == A_CNXN) {
    		/* CONNECT(version, maxdata, "system-id-string") */
#ifdef CONFIG_SYSTEM_ADB_AUTHENTICATION
        	send_auth_request(client, p);
#else
        	send_cnxn_frame(client, p);
        	client->is_connected = 1;
#endif /* CONFIG_SYSTEM_ADB_AUTHENTICATION */
        	return;
    	}

#ifdef CONFIG_SYSTEM_ADB_AUTHENTICATION
    	else if (p->msg.command == A_AUTH) {
    		handle_auth_frame(client, p);
    		return;
    	}
#endif /* CONFIG_SYSTEM_ADB_AUTHENTICATION */

    	goto invalid_frame;
    }

    /* Client is connected */

    switch(p->msg.command){
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

invalid_frame:
    adb_log("handle_packet: what is %08x?!\n", p->msg.command);
    adb_hal_apacket_release(client, p);
    client->ops->close(client);
}

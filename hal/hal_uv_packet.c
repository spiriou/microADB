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

#include <uv.h>
#include <stdlib.h>

#include "adb.h"
#include "hal_uv_priv.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

apacket* adb_hal_apacket_allocate(adb_client_t *c)
{
    apacket_uv_t *ap;
    adb_client_uv_t *client = container_of(c, adb_client_uv_t, client);
    ap = adb_uv_packet_allocate(client, 0);

    if (ap == NULL)
        return NULL;

    return &ap->p;
}

void adb_hal_apacket_release(adb_client_t *c, apacket *p) {
    adb_client_uv_t *client = container_of(c, adb_client_uv_t, client);
    apacket_uv_t *up = container_of(p, apacket_uv_t, p);

    /* Sanity check */
    assert(client->frame_count > 0);
    free(up);

    if (client->frame_count > CONFIG_ADBD_FRAME_MAX) {
        client->frame_count = CONFIG_ADBD_FRAME_MAX-1;
        /* kick may try to allocate packet again.
         * frame_count is CONFIG_ADBD_FRAME_MAX after first successful
         * allocation. In case another allocation fails, it will be
         * equal to CONFIG_ADBD_FRAME_MAX + 1 again.
         */
        c->ops->kick(c);
    }
    else {
        client->frame_count -= 1;
    }
}

apacket_uv_t* adb_uv_packet_allocate(adb_client_uv_t *client, int before_connect)
{
    apacket_uv_t* p;

    /* Limit frame allocation */
    if (client->frame_count >= CONFIG_ADBD_FRAME_MAX) {
        /* Keep track that at least one allocation has failed */
        client->frame_count = CONFIG_ADBD_FRAME_MAX + 1;
        return NULL;
    }

    if (before_connect) {
        p = (apacket_uv_t*)malloc(sizeof(apacket_uv_t)+
            CONFIG_ADBD_CNXN_PAYLOAD_SIZE-CONFIG_ADBD_PAYLOAD_SIZE);
    }
    else {
        p = (apacket_uv_t*)malloc(sizeof(apacket_uv_t));
    }

    if (p == NULL) {
      /* out of memory, stop adb server */
      adb_err("failed to allocate an apacket\n");
      /* FIXME calling client close() now may lead to memory corruption */
      client->client.ops->close(&client->client);
      return NULL;
    }

    client->frame_count += 1;
    return p;
}

void adb_uv_after_write(uv_write_t* req, int status) {
    apacket_uv_t *up = container_of(req, apacket_uv_t, wr);
    adb_client_uv_t *client = (adb_client_uv_t*)req->data;

    if (status < 0) {
        adb_err("write failed %d\n", status);
        adb_hal_apacket_release(&client->client, &up->p);

        client->client.ops->close(&client->client);
        return;
    }

    if (up->p.write_len > 0) {
        /* Send current packet payload (usually after an OKAY frame) */
        adb_send_data_frame(&client->client, &up->p);
        return;
    }

    adb_hal_apacket_release(&client->client, &up->p);
}

void adb_uv_allocate_frame(adb_client_uv_t *client, uv_buf_t* buf) {
    apacket_uv_t *ap;

    if (client->cur_packet) {
        /* Current frame not complete */

        ap = client->cur_packet;
        buf->base = &((char*)&ap->p.msg)[client->cur_len];

        if (client->cur_len < sizeof(ap->p.msg)) {
            buf->len = sizeof(ap->p.msg)-client->cur_len;
        }
        else {
            buf->len = sizeof(ap->p.msg)+ap->p.msg.data_length-client->cur_len;
        }
    }
    else {
        /* Try to allocate new frame */
        ap = adb_uv_packet_allocate(client, !client->client.is_connected);

        if (ap == NULL) {
            /* No available frames. Try again later */
            buf->len = 0;
            return;
        }

        /* Setup new packet */

        client->cur_packet = ap;
        client->cur_len = 0;
        buf->base = (char*)&ap->p.msg;
        buf->len = sizeof(ap->p.msg);
    }
}

void adb_uv_on_data_available(adb_client_uv_t *client, uv_stream_t *stream,
        ssize_t nread, const uv_buf_t* buf) {
    UNUSED(buf);

    apacket_uv_t *up;

    if (nread == UV_ENOBUFS) {
        /* No frame available, stop read events for now */
        uv_read_stop(stream);
        return;
    }

    if (nread == 0) {
        /* No data available. This should not happen. */
        if (client->cur_len <= 0) {
            /* Release memory waiting for next frame */
            adb_hal_apacket_release(&client->client, &client->cur_packet->p);
            client->cur_packet = NULL;
        }
        return;
    }
    if (nread < 0) {
        if (nread != UV_EOF) {
            adb_err("read failed %d\n", nread);
        }
        client->client.ops->close(&client->client);
        return;
    }

    up = client->cur_packet;
    assert(up);

    if (client->cur_len < sizeof(amessage)) {

        /* Validate frame header */

        if (client->cur_len+nread >= (int)sizeof(amessage) && (
            (!client->client.is_connected && adb_check_auth_frame_header(&up->p)) ||
            (client->client.is_connected && adb_check_frame_header(&up->p)))) {
            adb_err("bad header: terminated (data)\n");
            client->client.ops->close(&client->client);
            return;
        }
    }

    client->cur_len += nread;

    if (client->cur_len < sizeof(amessage)+up->p.msg.data_length) {
        /* Packet not fully received */
        return;
    }

    /* Check data */

    if(adb_check_frame_data(&up->p)) {
        adb_err("bad data: terminated (data)\n");
        client->client.ops->close(&client->client);
        return;
    }

    /* Frame received, process it */

    client->cur_packet = NULL;
    adb_process_packet(&client->client, &up->p);
}

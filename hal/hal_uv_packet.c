#include <uv.h>
#include <stdlib.h>

#include "adb.h"
#include "hal_uv_priv.h"

void adb_hal_apacket_release(adb_client_t *c, apacket *p) {
    adb_client_uv_t *client = container_of(c, adb_client_uv_t, client);
    apacket_uv_t *up = container_of(p, apacket_uv_t, p);

    adb_log("entry frame_count %d\n", client->frame_count);

    /* Sanity check */

    assert(client->frame_count > 0);

    if (client->frame_count > CONFIG_SYSTEM_ADB_FRAME_MAX) {
    	client->frame_count = CONFIG_SYSTEM_ADB_FRAME_MAX-1;
    	c->ops->kick(c);
    }
    else {
        client->frame_count -= 1;
    }

    adb_log("%p\n", &up->p);
    free(up);
}

apacket_uv_t* adb_uv_packet_allocate(adb_client_uv_t *client, int is_connect)
{
    apacket_uv_t* p;

    /* Limit frame allocation */
    if (client->frame_count >= CONFIG_SYSTEM_ADB_FRAME_MAX) {
    	client->frame_count = CONFIG_SYSTEM_ADB_FRAME_MAX + 1;
        return NULL;
    }

    if (is_connect) {
        p = (apacket_uv_t*)malloc(sizeof(apacket_uv_t)+
            CONFIG_ADB_CNXN_PAYLOAD_SIZE-CONFIG_ADB_PAYLOAD_SIZE);
    }
    else {
        p = (apacket_uv_t*)malloc(sizeof(apacket_uv_t));
    }

    if (p == NULL) {
      // FIXME
      adb_log("failed to allocate an apacket\n");
      client->client.ops->close(&client->client);
      // fatal("failed to allocate an apacket");
      return NULL;
    }

    client->frame_count += 1;
    adb_log("frame_count=%d %p\n", client->frame_count, &p->p);
    return p;
}

void adb_uv_after_write(uv_write_t* req, int status) {
    apacket_uv_t *up = container_of(req, apacket_uv_t, wr);
    adb_client_uv_t *client = (adb_client_uv_t*)req->data;

    // adb_log("entry %p\n", &up->p);

    if (status < 0) {
        adb_log("failed %d\n", status);
        adb_hal_apacket_release(&client->client, &up->p);

        client->client.ops->close(&client->client);
        return;
    }

    if (up->p.write_len > 0) {
        adb_log("restart\n");
        adb_client_send_service_payload(&client->client, &up->p);
        return;
    }

    adb_hal_apacket_release(&client->client, &up->p);
}

void adb_uv_allocate_frame(adb_client_uv_t *client, uv_buf_t* buf) {
    apacket_uv_t *ap;

    adb_log("entry %p %d\n", client->cur_packet, client->frame_count);

    if (client->cur_packet) {
        /* Current frame not complete */

        ap = client->cur_packet;
        buf->base = &((char*)&ap->p.msg)[client->cur_len];

        // adb_log("realloc %d\n", client->cur_len);
        if (client->cur_len < sizeof(ap->p.msg)) {
            buf->len = sizeof(ap->p.msg)-client->cur_len;
        }
        else {
            buf->len = sizeof(ap->p.msg)+ap->p.msg.data_length-client->cur_len;
            if (!client->client.is_connected) {
                // buf->len = sizeof(ap->p.msg)+CONFIG_ADB_CNXN_PAYLOAD_SIZE-client->cur_len;
                assert(ap->p.msg.data_length <= CONFIG_ADB_CNXN_PAYLOAD_SIZE);
            }
            else {
                // buf->len = sizeof(ap->p.msg)+CONFIG_ADB_PAYLOAD_SIZE-client->cur_len;
                assert(ap->p.msg.data_length <= CONFIG_ADB_PAYLOAD_SIZE);
            }
        }
    }
    else {
        /* Size of frames for authentication must be > 256 */
        ap = adb_uv_packet_allocate(client, !client->client.is_connected);

        if (ap == NULL) {
            adb_log("frame allocation failed\n");
            buf->len = 0;
            /* Wait for available memory */
            // client->client.ops->close(&client->client);
            return;
        }

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
        adb_log("STOP READ EVENT %d\n", client->frame_count);
        uv_read_stop(stream);
        // client->frame_count = -1;
        return;
    }

    if (nread == 0) {
        /* No data available. This should not happen. FIXME */
        adb_log("READ RETURNED NO DATA. FIXME %d %p\n", client->cur_len, client->cur_packet);
        if (client->cur_len <= 0) {
            /* Release memory waiting for next frame */
            adb_hal_apacket_release(&client->client, &client->cur_packet->p);
            client->cur_packet = NULL;
        }
        return;
    }
    if (nread <= 0) {
        adb_log("failed %d\n", nread);
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
            adb_log("bad header: terminated (data)\n");
            DumpHex(&up->p.msg, sizeof(amessage));
            client->client.ops->close(&client->client);
            return;
        }
    }

    client->cur_len += nread;

    if (client->cur_len < sizeof(amessage)+up->p.msg.data_length) {
        return;
    }

    /* Check data */

    if(adb_check_frame_data(&up->p)) {
        adb_log("bad data: terminated (data)\n");
        DumpHex(&up->p.msg, up->p.msg.data_length+sizeof(amessage));
        client->client.ops->close(&client->client);
        return;
    }

    /* Frame received, process it */

    client->cur_packet = NULL;
    adb_process_packet(&client->client, &up->p);
}

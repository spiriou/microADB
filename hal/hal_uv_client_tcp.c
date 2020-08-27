#include <arpa/inet.h>

#include "adb.h"
#include "hal_uv_priv.h"
#include <uv.h>

static void adb_uv_allocate_frame(uv_handle_t* handle,
                       size_t suggested_size, uv_buf_t* buf);
static void tcp_uv_close(adb_client_t *s);
static void tcp_on_data_available(uv_stream_t* handle, 
        ssize_t nread, const uv_buf_t* buf);
static void tcp_after_write(uv_write_t* req, int status);

static void adb_uv_allocate_frame(uv_handle_t* handle,
                       size_t suggested_size,
                       uv_buf_t* buf) {
    UNUSED(handle);
    UNUSED(suggested_size);

    apacket_uv_t *ap;
    adb_client_tcp_t *client = container_of(handle, adb_client_tcp_t, socket);

    if (client->cur_packet) {
        /* Current frame not complete */

        ap = client->cur_packet;
        buf->base = &((char*)&ap->p.msg)[client->cur_len];

        // adb_log("realloc %d\n", client->cur_len);
        if (client->cur_len < sizeof(ap->p.msg)) {
            buf->len = sizeof(ap->p.msg)-client->cur_len;
        }
        else {
            if (!client->client.is_connected) {
                buf->len = sizeof(ap->p.msg)+CONFIG_ADB_CNXN_PAYLOAD_SIZE-client->cur_len;
            }
            else {
                buf->len = sizeof(ap->p.msg)+CONFIG_ADB_PAYLOAD_SIZE-client->cur_len;
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

static void tcp_on_data_available(uv_stream_t* handle, 
        ssize_t nread, const uv_buf_t* buf) {
    UNUSED(buf);

    apacket_uv_t *up;
    adb_client_tcp_t *client = container_of(handle, adb_client_tcp_t, socket);

    if (nread == UV_ENOBUFS) {
        adb_log("STOP READ EVENT %d\n", client->frame_count);
        uv_read_stop((uv_stream_t*)&client->socket);
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
        tcp_uv_close(&client->client);
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
            tcp_uv_close(&client->client);
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
        tcp_uv_close(&client->client);
        return;
    }

    /* Frame received, process it */

    client->cur_packet = NULL;
    adb_process_packet(&client->client, &up->p);
}



static void tcp_after_write(uv_write_t* req, int status) {
    apacket_uv_t *up = container_of(req, apacket_uv_t, wr);
    adb_client_tcp_t *client = (adb_client_tcp_t*)req->data;

    adb_log("entry %p\n", &up->p);

    if (status < 0) {
        adb_log("failed %d\n", status);
        adb_hal_apacket_release(&client->client, &up->p);

        tcp_uv_close(&client->client);
        return;
    }

    if (up->p.write_len > 0) {
        adb_client_send_service_payload(&client->client, &up->p);
        return;
    }

    adb_hal_apacket_release(&client->client, &up->p);
}

static int tcp_uv_write(adb_client_t *c, apacket *p) {
    uv_buf_t buf;
    apacket_uv_t *up = container_of(p, apacket_uv_t, p);
    adb_client_tcp_t *client = container_of(c, adb_client_tcp_t, client);

    buf = uv_buf_init((char*)&p->msg,
        sizeof(p->msg) + p->msg.data_length);

    /* Packet is now tracked by libuv */
    up->wr.data = client;

    if (uv_write(&up->wr, (uv_stream_t*)&client->socket, &buf, 1, tcp_after_write)) {
        adb_log("write %d %p %d\n", buf.len, buf.base, client->socket.io_watcher.fd);
        fatal("uv_write failed");
        return -1;
    }

    return 0;
}

static void tcp_uv_kick(adb_client_t *c) {
    adb_client_tcp_t *client = container_of(c, adb_client_tcp_t, client);

    if (!uv_is_active((uv_handle_t*)&client->socket)) {
        adb_log("RESTART READ EVENTS\n");
        /* Restart read events */
        int ret = uv_read_start((uv_stream_t*)&client->socket,
            adb_uv_allocate_frame,
            tcp_on_data_available);
        /* TODO check return code */
        assert(ret == 0);
    }

    adb_client_kick_services(c);
}

static void tcp_uv_close(adb_client_t *c) {
    adb_client_tcp_t *client = (adb_client_tcp_t*)c;

    if (client->cur_packet) {
        adb_hal_apacket_release(&client->client, &client->cur_packet->p);
        client->cur_packet = NULL;
    }

    /* Close socket and cancel all pending write requests if any */
    uv_close((uv_handle_t*)&client->socket, NULL);
    adb_destroy_client(c);
}

static const adb_client_ops_t adb_tcp_uv_ops = {
    .write = tcp_uv_write,
    .kick  = tcp_uv_kick,
    .close = tcp_uv_close
};

static void tcp_on_connection(uv_stream_t* server, int status) {
    int ret;
    adb_client_tcp_t *client;
    adb_context_uv_t *adbd = (adb_context_uv_t*)server->data;

    if (status < 0) {
        adb_log("connect failed %d\n", status);
        return;
    }

    client = (adb_client_tcp_t*)adb_create_client(sizeof(*client));
    if (client == NULL) {
        adb_log("failed to allocate stream\n");
        return;
    }

    /* Setup adb_client */
    client->cur_packet = NULL;
    client->frame_count = 0;
    client->client.ops = &adb_tcp_uv_ops;

    ret = uv_tcp_init(adbd->loop, &client->socket);
    assert(ret == 0);

    client->socket.data = server;


    ret = uv_accept(server, (uv_stream_t*)&client->socket);
    /* TODO check return code */
    assert(ret == 0);

    ret = uv_read_start((uv_stream_t*)&client->socket,
        adb_uv_allocate_frame,
        tcp_on_data_available);
    /* TODO check return code */
    assert(ret == 0);

    /* Insert client in context */
    client->client.next = adbd->context.clients;
    adbd->context.clients = &client->client;
}

int tcp_setup_server(adb_context_uv_t *adbd) {
    int ret;
    struct sockaddr_in addr;

    ret = uv_tcp_init(adbd->loop, &adbd->tcp_server);
    adbd->tcp_server.data = adbd;
    if (ret) {
        /* TODO: Error codes */
        adb_log("tcp server init error %d %d\n", ret, errno);
        return ret;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(CONFIG_ADB_TCP_PORT);

    ret = uv_tcp_bind(&adbd->tcp_server, (const struct sockaddr*)&addr, 0);
    if (ret) {
        /* TODO: Error codes */
        adb_log("tcp server bind error %d %d\n", ret, errno);
        return ret;
    }

    ret = uv_listen((uv_stream_t*)&adbd->tcp_server, 5, tcp_on_connection);
    if (ret) {
        /* TODO: Error codes */
        adb_log("tcp server listen error %d %d\n", ret, errno);
        return ret;
    }
    return 0;
}

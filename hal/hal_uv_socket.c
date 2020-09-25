#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "adb.h"
#include "tcp_service.h"
#include "hal_uv_priv.h"

static void tcp_stream_allocate_frame(uv_handle_t* handle,
                       size_t suggested_size,
                       uv_buf_t* buf) {
    UNUSED(suggested_size);

    apacket_uv_t *up;
    // adb_tcp_socket_t *socket = container_of(handle, adb_tcp_socket_t, handle);
    adb_client_t *client = (adb_client_t*)handle->data;

    up = adb_uv_packet_allocate((adb_client_tcp_t*)client, 0);
    if (up == NULL) {
        adb_log("frame allocation failed\n");
        buf->len = 0;
        return;
    }

    adb_log("entry %d\n", suggested_size);
    buf->base = (char*)up->p.data;
    buf->len = CONFIG_ADB_PAYLOAD_SIZE;
}

static void tcp_stream_on_data_available(uv_stream_t* handle, 
        ssize_t nread, const uv_buf_t* buf) {
    UNUSED(handle);
    UNUSED(nread);
    UNUSED(buf);

    apacket_uv_t *ap = container_of(buf->base, apacket_uv_t, p.data);
    adb_tcp_socket_t *socket = container_of(handle, adb_tcp_socket_t, handle);
    adb_client_t *client = (adb_client_t*)socket->handle.data;

    adb_log("entry %d %p\n", nread, &ap->p);

    if (nread == UV_ENOBUFS) {
        uv_read_stop((uv_stream_t*)&socket->handle);
        return;
    }

    if (nread == 0) {
        /* It happens sometimes */
        adb_hal_apacket_release(client, &ap->p);
        return;
    }

    if (nread > 0) {
        uv_read_stop((uv_stream_t*)&socket->handle);
        ap->p.msg.data_length = nread;
    }
    else {
        // ERROR
        ap->p.msg.data_length = 0;
    }

    socket->on_data_cb(socket, &ap->p);
}

static void socket_close_cb(uv_handle_t* handle) {
    adb_log("entry %p\n", handle);

    adb_tcp_socket_t *socket = container_of(handle, adb_tcp_socket_t, handle);
    socket->close_cb(socket);
}

void adb_hal_socket_close(adb_tcp_socket_t *socket, void (*close_cb)(adb_tcp_socket_t*)) {
    socket->close_cb = close_cb;
    uv_close((uv_handle_t*)&socket->handle, socket_close_cb);
}

int adb_hal_socket_start(adb_tcp_socket_t *socket,
    void (*on_data_cb)(adb_tcp_socket_t*, apacket*)) {
    adb_log("entry\n");

    socket->on_data_cb = on_data_cb;

    if (!uv_is_active((uv_handle_t*)&socket->handle)) {
        assert(0 == uv_read_start((uv_stream_t*)&socket->handle,
                                tcp_stream_allocate_frame,
                                tcp_stream_on_data_available));
    }

    return 0;
}

int adb_hal_socket_stop(adb_tcp_socket_t *socket) {
    return uv_read_stop((uv_stream_t*)&socket->handle);
}

static void fwd_tcp_after_write(uv_write_t* req, int status) {
    apacket_uv_t *up = container_of(req, apacket_uv_t, wr);
    adb_tcp_socket_t *socket = (adb_tcp_socket_t*)req->data;

    adb_client_t *client = (adb_client_t*)socket->handle.data;

    if (status < 0) {
        adb_log("failed %d\n", status);
        socket->on_write_cb(client, socket, NULL);
        // Close socket service
        // adb_service_close(client, adb_service_t *svc);
        // tcp_uv_close(&client->client);
        adb_hal_apacket_release(client, &up->p);
        return;
    }

    socket->on_write_cb(client, socket, &up->p);
}

int adb_hal_socket_write(adb_tcp_socket_t *socket, apacket *p,
    void (*cb)(adb_client_t*, adb_tcp_socket_t*, apacket*)) {
    uv_buf_t buf;
    apacket_uv_t *uv_p = container_of(p, apacket_uv_t, p);

    buf = uv_buf_init((char*)&p->data,
        p->msg.data_length);

    /* Packet is now tracked by libuv */
    uv_p->wr.data = socket;
    socket->on_write_cb = cb;

    if (uv_write(&uv_p->wr, (uv_stream_t*)&socket->handle, &buf, 1, fwd_tcp_after_write)) {
        adb_log("write %d %p %d\n", buf.len, buf.base, socket->handle.io_watcher.fd);
        fatal("uv_write failed");
        return -1;
    }

    return 0;
}
























static void connect_cb(uv_connect_t* req, int status) {
    assert(0 == status);

    // apacket_uv_t *ap;
    adb_tcp_fstream_t *stream = container_of(req, adb_tcp_fstream_t, connect_req);
    stream->on_connect_cb(stream, status);

    // adb_client_t *client = (adb_client_t*)stream->socket.handle.data;

#if 0
    ap = adb_uv_packet_allocate((adb_client_tcp_t*)client, 0); //  adb_uv_tcp_client_restart_packet, 0);
    if (ap == NULL) {
        adb_log("frame allocation failed\n");
        // TODO close service
        // tcp_uv_close(&client->client);
        return;
    }
#endif
    // ap->priv = &stream->socket;
    // stream->socket.p = &ap->p;

    // assert(0 == uv_read_start((uv_stream_t*)&stream->socket.handle,
    //                         tcp_stream_allocate_frame,
    //                         tcp_client_on_data_available));
}

int adb_hal_socket_connect(adb_client_t *client, adb_tcp_fstream_t *stream, int port,
    void (*on_connect_cb)(adb_tcp_fstream_t*, int)) {
    adb_log("entry\n");

    uv_handle_t *handle = (uv_handle_t*)(client+1);

    adb_log("handle %p %p\n", handle->loop, handle->data);

    struct sockaddr_in addr;
    // stream->connect_req.data = stream;

    assert(0 == uv_ip4_addr("127.0.0.1", port, &addr));
    assert(0 == uv_tcp_init(handle->loop, &stream->socket.handle));


    stream->socket.handle.data = client;
    stream->on_connect_cb = on_connect_cb;

    return uv_tcp_connect(&stream->connect_req,
                          &stream->socket.handle,
                          (const struct sockaddr*) &addr,
                          connect_cb);
}

static void tcp_server_on_connection(uv_stream_t* server, int status) {
    int ret;
    // adb_tcp_server_t *socket =
    //     (adb_tcp_server_t*)container_of(server, adb_tcp_server_t, handle);
    apacket_uv_t *ap;
    adb_client_t *client = (adb_client_t*)server->data;
    adb_rstream_service_t* rsvc;

    adb_log("entry %d\n", status);

    if (status != 0) {
        adb_log("Connect error %s\n", uv_err_name(status));
    }
    assert(status == 0);

    rsvc = tcp_allocate_rstream_service(client);
    if (rsvc == NULL) {
        fatal("out of memory");
    }

    assert(0 == uv_tcp_init(server->loop, &rsvc->stream.socket.handle));
    rsvc->stream.socket.handle.data = client;

    ret = uv_accept(server, (uv_stream_t*)&rsvc->stream.socket.handle);
    if (ret) {
        // FIXME
        adb_service_close(client, &rsvc->service, NULL);
        return;
    }

#if 1
    ap = adb_uv_packet_allocate((adb_client_tcp_t*)client, 0); // NULL, 0);
    if (ap == NULL) {
        adb_log("frame allocation failed\n");
        // TODO
        // uv_close((uv_handle_t*)&stream->stream.handle, tcp_close_server);
        // adb_service_close(client, &stream->service);
        return;
    }

    // rsvc->stream.socket.p = NULL;
#endif

    // FIXME
    ret = sprintf((char*)ap->p.data, "tcp:%d", 7100);
    send_open_frame(client, &ap->p,
        rsvc->service.id,
        0, // rsvc->service.peer_id,
        ret+1);

    // Wait for ACK to open socket

    /* TODO */
    // setup tcp
    // send OPEN
    // register service to client
    // r = uv_read_start(stream, echo_alloc, after_read);
    // ASSERT(r == 0);
}

int adb_hal_socket_listen(adb_client_t *client, adb_tcp_server_t *socket, int port) {
    int ret;
    adb_log("entry\n");

    uv_handle_t *handle = (uv_handle_t*)(client+1);

    adb_log("handle %p %p\n", handle->loop, handle->data);

    struct sockaddr_in addr;

    assert(0 == uv_ip4_addr("0.0.0.0", port, &addr));
    assert(0 == uv_tcp_init(handle->loop, &socket->handle));
    assert(0 == uv_tcp_bind(&socket->handle, (const struct sockaddr*) &addr, 0));

    socket->handle.data = client;

    ret = uv_listen((uv_stream_t*)&socket->handle,
        SOMAXCONN, tcp_server_on_connection);
    if (ret) {
        adb_log("listen failed %d\n", ret);
        return -1;
    }

    return 0;
}

#include "adb.h"
#include "hal_uv_priv.h"
#include <uv.h>

typedef struct adb_client_usb_s {
    adb_client_uv_t uc;
    /* libuv handle must be right after adb_client_uv_t */
    uv_pipe_t pipe;
} adb_client_usb_t;

// static void adb_uv_allocate_frame(uv_handle_t* handle,
//                        size_t suggested_size, uv_buf_t* buf);
static void usb_uv_close(adb_client_t *s);
// static void usb_on_data_available(uv_stream_t* handle, 
//         ssize_t nread, const uv_buf_t* buf);
// static void usb_after_write(uv_write_t* req, int status);

static void usb_uv_allocate_frame(uv_handle_t* handle,
                       size_t suggested_size, uv_buf_t* buf) {
    UNUSED(handle);
    UNUSED(suggested_size);

    adb_client_usb_t *client = container_of(handle, adb_client_usb_t, pipe);
    adb_uv_allocate_frame(&client->uc, buf);
}

static void usb_uv_on_data_available(uv_stream_t* handle, 
        ssize_t nread, const uv_buf_t* buf) {
    UNUSED(buf);

    adb_client_usb_t *client = container_of(handle, adb_client_usb_t, pipe);

    adb_uv_on_data_available(&client->uc, (uv_stream_t*)handle, nread, buf);
}

#if 1
static int usb_uv_write(adb_client_t *c, apacket *p) {
    uv_buf_t buf[2];
    int buf_cnt;
    apacket_uv_t *up = container_of(p, apacket_uv_t, p);
    adb_client_usb_t *client = container_of(c, adb_client_usb_t, uc.client);

    // buf[0] = uv_buf_init((char*)&p->msg,
    //     sizeof(p->msg)); //  + p->msg.data_length);
    // buf[1] = uv_buf_init((char*)&p->data,
    //     p->msg.data_length);


    buf[0].base = (char*)&p->msg;
    buf[0].len  = sizeof(p->msg);

    if (p->msg.data_length > 0) {
        buf[1].base = (char*)p->data;
        buf[1].len  = p->msg.data_length;
        buf_cnt = 2;
    }
    else {
        buf_cnt = 1;
    }

    /* Packet is now tracked by libuv */
    up->wr.data = &client->uc;

    if (uv_write(&up->wr, (uv_stream_t*)&client->pipe, buf, buf_cnt, adb_uv_after_write)) {
        // adb_log("write %d %p %d\n", buf.len, buf.base, client->pipe.io_watcher.fd);
        fatal("uv_write failed");
        return -1;
    }

    return 0;
}
#else
static void adb_uv_after_header_write(uv_write_t* req, int status) {
    apacket_uv_t *up = container_of(req, apacket_uv_t, wr);
    adb_client_uv_t *client = (adb_client_uv_t*)req->data;

    adb_log("entry %p\n", &up->p);

    if (status < 0) {
        adb_log("failed %d\n", status);
        adb_hal_apacket_release(&client->client, &up->p);

        client->client.ops->close(&client->client);
        return;
    }
    uv_buf_t buf;
    apacket_uv_t *up = container_of(p, apacket_uv_t, p);
    adb_client_usb_t *client = container_of(c, adb_client_usb_t, uc.client);

    buf.base = (char*)p->data;
    buf.len  = p->msg.data_length;

    /* Packet is now tracked by libuv */
    up->wr.data = &client->uc;

    if (uv_write(&up->wr, (uv_stream_t*)&client->pipe, &buf, 1, adb_uv_after_write)) {
        // adb_log("write %d %p %d\n", buf.len, buf.base, client->pipe.io_watcher.fd);
        fatal("uv_write failed");
        // return -1;
    }
}

static int usb_uv_write(adb_client_t *c, apacket *p) {
    uv_buf_t buf;
    apacket_uv_t *up = container_of(p, apacket_uv_t, p);
    adb_client_usb_t *client = container_of(c, adb_client_usb_t, uc.client);

    buf.base = (char*)&p->msg;
    buf.len  = sizeof(p->msg);

    /* Packet is now tracked by libuv */
    up->wr.data = &client->uc;

    if (uv_write(&up->wr, (uv_stream_t*)&client->pipe, &buf, 1, adb_uv_after_header_write)) {
        // adb_log("write %d %p %d\n", buf.len, buf.base, client->pipe.io_watcher.fd);
        fatal("uv_write failed");
        return -1;
    }

    return 0;
}
#endif

static void usb_uv_kick(adb_client_t *c) {
    adb_client_usb_t *client = container_of(c, adb_client_usb_t, uc.client);

    if (!uv_is_active((uv_handle_t*)&client->pipe)) {
        adb_log("RESTART READ EVENTS\n");
        /* Restart read events */
        int ret = uv_read_start((uv_stream_t*)&client->pipe,
            usb_uv_allocate_frame,
            usb_uv_on_data_available);
        /* TODO check return code */
        assert(ret == 0);
    }

    adb_client_kick_services(c);
}

static void usb_uv_close(adb_client_t *c) {
    adb_client_usb_t *client = (adb_client_usb_t*)c;

    /* Close pipe and cancel all pending write requests if any */
    uv_close((uv_handle_t*)&client->pipe, NULL);

    adb_uv_close_client(&client->uc);
}

static const adb_client_ops_t adb_usb_uv_ops = {
    .write = usb_uv_write,
    .kick  = usb_uv_kick,
    .close = usb_uv_close
};

int adb_uv_usb_setup(adb_context_uv_t *adbd, const char *path) {
    int ret;
    int fd;
    adb_client_usb_t *client;

    adb_log("entry\n");

    client = (adb_client_usb_t*)adb_uv_create_client(sizeof(*client));
    if (client == NULL) {
        adb_log("failed to allocate usb client\n");
        return -ENOMEM;
    }

    /* Setup adb_client */

    client->uc.client.ops = &adb_usb_uv_ops;

    ret = uv_pipe_init(adbd->loop, &client->pipe, 0);
    client->pipe.data = adbd;
    if (ret) {
        /* TODO: Error codes */
        adb_log("usb init error %d %d\n", ret, errno);
        return ret;
    }

    fd = open(path, O_RDWR | O_CLOEXEC);
    adb_log("Open FD %d %d\n", fd, errno);
    if (fd < 0) {
        adb_log("failed to open usb device %d %d\n", fd, errno);
        return fd;
    }

    ret = uv_pipe_open(&client->pipe, fd);
    if (ret) {
        /* TODO: Error codes */
        adb_log("usb pipe open error %d %d\n", ret, errno);
        close(fd);
        return ret;
    }

    ret = uv_read_start((uv_stream_t*)&client->pipe,
        usb_uv_allocate_frame,
        usb_uv_on_data_available);
    /* TODO check return code */
    assert(ret == 0);

    /* Insert client in context */
    client->uc.client.next = adbd->context.clients;
    adbd->context.clients = &client->uc.client;
    adb_log("OK\n");

    return 0;
}

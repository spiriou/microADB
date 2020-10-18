#ifndef __ADB_HAL_UV_PRIV_H__
#define __ADB_HAL_UV_PRIV_H__

#include <uv.h>
#include "adb.h"

/* Private structures */

typedef struct apacket_uv_s
{
    uv_write_t wr;
    // void (*release)(adb_client_t *client, struct apacket_uv_s*);
    void *priv;
    apacket p;
} apacket_uv_t;

typedef struct adb_client_uv_s {
    adb_client_t client;
    /* Frame allocation management */
    struct apacket_uv_s *cur_packet;
    unsigned int cur_len;
    int frame_count;
    /* Events handling */
    // TODO
} adb_client_uv_t;

typedef struct adb_context_uv_s {
    adb_context_t context;
#ifdef __NUTTX__
    uv_context_t uv_context;
#endif
    uv_loop_t *loop;
#ifdef CONFIG_SYSTEM_ADB_TCP_SERVER
    uv_tcp_t tcp_server;
#endif
// #ifdef CONFIG_SYSTEM_ADB_USB_SERVER
//     adb_client_usb_t usb_client;
// #endif
} adb_context_uv_t;

int adb_uv_tcp_setup(adb_context_uv_t *adbd);
int adb_uv_usb_setup(adb_context_uv_t *adbd, const char *path);

// void adb_uv_release_packet(adb_client_t *client, apacket_uv_t *p);
// apacket_uv_t* adb_hal_apacket_uv_allocate(void (*release_cb)(adb_client_t*, struct apacket_uv_s*), int is_auth);

apacket_uv_t* adb_uv_packet_allocate(
    adb_client_uv_t *client,
    int is_connect);
void adb_uv_packet_release(adb_client_uv_t *c, apacket_uv_t *p);

void adb_uv_allocate_frame(adb_client_uv_t *client, uv_buf_t* buf);
void adb_uv_after_write(uv_write_t* req, int status);
adb_client_t* adb_uv_create_client(size_t size);
void adb_uv_on_data_available(adb_client_uv_t *client, uv_stream_t *stream,
        ssize_t nread, const uv_buf_t* buf);
void adb_uv_close_client(adb_client_uv_t *client);

#endif /* __ADB_HAL_UV_PRIV_H__ */

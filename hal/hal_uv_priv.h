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

typedef struct adb_context_uv_s {
    adb_context_t context;
#ifdef __NUTTX__
    uv_context_t uv_context;
#endif
    uv_loop_t *loop;
#ifdef CONFIG_ADB_TCP_SERVER
    uv_tcp_t tcp_server;
#endif
} adb_context_uv_t;

/* FIXME move adb_client_tcp_t in implementation file */

typedef struct adb_client_tcp_s {
    adb_client_t client;
    /* libuv handle must be right after adb_client_t */
    uv_tcp_t socket;
    struct apacket_uv_s *cur_packet;
    unsigned int cur_len;
    int frame_count;
} adb_client_tcp_t;

int tcp_setup_server(adb_context_uv_t *adbd);
// void adb_uv_release_packet(adb_client_t *client, apacket_uv_t *p);
// apacket_uv_t* adb_hal_apacket_uv_allocate(void (*release_cb)(adb_client_t*, struct apacket_uv_s*), int is_auth);

apacket_uv_t* adb_uv_packet_allocate(
    adb_client_tcp_t *client,
    int is_connect);
void adb_uv_packet_release(adb_client_t *c, apacket_uv_t *p);

#endif /* __ADB_HAL_UV_PRIV_H__ */

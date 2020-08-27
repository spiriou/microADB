#include <uv.h>
#include <stdlib.h>

#include "adb.h"
#include "hal_uv_priv.h"

void adb_hal_apacket_release(adb_client_t *c, apacket *p) {
    apacket_uv_t *up = container_of(p, apacket_uv_t, p);
    adb_client_tcp_t *client = container_of(c, adb_client_tcp_t, client);

    adb_log("entry frame_count %d\n", client->frame_count);

    /* Sanity check */
    assert(client->frame_count > 0);

    if (client->frame_count > CONFIG_SYSTEM_ADB_TCP_FRAME_MAX) {
    	client->frame_count = CONFIG_SYSTEM_ADB_TCP_FRAME_MAX-1;
    	c->ops->kick(c);
    }
    else {
        client->frame_count -= 1;
    }

    adb_log("%p\n", &up->p);
    free(up);
}

apacket_uv_t* adb_uv_packet_allocate(adb_client_tcp_t *client, int is_connect)
{
    apacket_uv_t* p;

    /* Limit frame allocation */
    if (client->frame_count >= CONFIG_SYSTEM_ADB_TCP_FRAME_MAX) {
    	client->frame_count = CONFIG_SYSTEM_ADB_TCP_FRAME_MAX + 1;
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

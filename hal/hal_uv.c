#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "adb.h"
#include "hal_uv_priv.h"
#include <uv.h>

static adb_context_uv_t g_adb_context;

adb_context_t* adb_hal_create_context() {
    adb_context_uv_t *adbd = &g_adb_context;

#ifdef __NUTTX__
    uv_library_init(&adbd->uv_context);
    adbd->loop = uv_default_loop(&adbd->uv_context);
#else
    adbd->loop = uv_default_loop();
#endif
    adbd->context.clients = NULL;

#ifdef CONFIG_ADB_TCP_SERVER
    if (tcp_setup_server(adbd)) {
        goto exit_fail;
    }
#endif

    return &adbd->context;

exit_fail:
#ifdef __NUTTX__
    uv_library_shutdown(&adbd->uv_context);
#endif
    return NULL;
}

void adb_hal_destroy_context(adb_context_t *context) {
    UNUSED(context);
}

adb_client_t *adb_hal_create_client(size_t size) {
    return (adb_client_t*)malloc(size);
}

void adb_hal_destroy_client(adb_client_t *client) {
    free(client);
}

int adb_hal_run(adb_context_t *context) {
    int ret;
    adb_context_uv_t *adbd =
        container_of(context, adb_context_uv_t, context);

    ret = uv_run(adbd->loop, UV_RUN_DEFAULT);
    adb_log("uv_loop exit %d\n", ret);

    return 0;
}

int adb_hal_random(void *buf, size_t len) {
    return uv_random(NULL, NULL, buf, len, 0, NULL);
}

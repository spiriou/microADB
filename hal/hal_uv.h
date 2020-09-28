#ifndef __ADB_HAL_UV_H__
#define __ADB_HAL_UV_H__

#include <uv.h>

struct apacket_s;
struct adb_client_s;

struct adb_event_s {
    uv_poll_t handle;
};

struct adb_pipe_s {
    uv_poll_t handle;
    int write_fd;
    // int std_fd[2];
    int uid; // FIXME remove ???
    void (*close_cb)(struct adb_pipe_s*);
    void (*on_data_cb)(struct adb_pipe_s*, struct apacket_s*);
};

#define ADB_STREAM_CONNECTED (1<<0)
#define ADB_STREAM_WAIT_ACK  (1<<1)

struct adb_tcp_socket_s {
    uv_tcp_t handle;
    void (*close_cb)(struct adb_tcp_socket_s*);
    void (*on_data_cb)(struct adb_tcp_socket_s*, struct apacket_s*);
    void (*on_write_cb)(struct adb_client_s*, struct adb_tcp_socket_s*, struct apacket_s*);
    // struct apacket_s *p;
};

struct adb_tcp_fstream_s {
    struct adb_tcp_socket_s socket;
    uint8_t flags;
    uv_connect_t connect_req;
    void (*on_connect_cb)(struct adb_tcp_fstream_s*, int);
};

struct adb_tcp_rstream_s {
    struct adb_tcp_socket_s socket;
    uint8_t flags;
};

struct adb_tcp_server_s {
    uv_tcp_t handle;
};

#endif /* __ADB_HAL_UV_H__ */

#include "adb.h"

static int adb_check_frame_magic(apacket *p)
{
    if(p->msg.magic != (p->msg.command ^ 0xffffffff)) {
        adb_log("invalid frame magic\n");
        return -1;
    }

    return 0;
}

int adb_check_frame_header(apacket *p)
{
    int ret;
    if ((ret = adb_check_frame_magic(p))) {
        return ret;
    }

    if(p->msg.data_length > CONFIG_ADB_PAYLOAD_SIZE) {
        adb_log("invalid frame size %d\n", p->msg.data_length);
        return -1;
    }

    return 0;
}

int adb_check_auth_frame_header(apacket *p)
{
    int ret;
    if ((ret = adb_check_frame_magic(p))) {
        return ret;
    }

    if(p->msg.data_length > CONFIG_ADB_CNXN_PAYLOAD_SIZE) {
        adb_log("invalid frame size %d\n", p->msg.data_length);
        return -1;
    }

    return 0;
}

int adb_check_frame_data(apacket *p)
{
    unsigned count, sum;
    unsigned char *x;

    count = p->msg.data_length;
    x = p->data;
    sum = 0;
    while(count-- > 0) {
        sum += *x++;
    }

    if(sum != p->msg.data_check) {
        return -1;
    } else {
        return 0;
    }
}
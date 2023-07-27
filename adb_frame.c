/*
 * Copyright (C) 2020 Simon Piriou. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

 #include "adb.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int adb_check_frame_magic(apacket *p)
{
    if(p->msg.magic != (p->msg.command ^ 0xffffffff)) {
        adb_err("invalid frame magic\n");
        return -1;
    }

    return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int adb_check_frame_header(apacket *p)
{
    int ret;
    if ((ret = adb_check_frame_magic(p))) {
        return ret;
    }

    if(p->msg.data_length > CONFIG_ADBD_PAYLOAD_SIZE) {
        adb_err("invalid frame size %d\n", p->msg.data_length);
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

    if(p->msg.data_length > CONFIG_ADBD_CNXN_PAYLOAD_SIZE) {
        adb_err("invalid frame size %d\n", p->msg.data_length);
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
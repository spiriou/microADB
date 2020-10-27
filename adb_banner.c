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
 * Public Functions
 ****************************************************************************/

int adb_fill_connect_data(char *buf, size_t bufsize)
{
    size_t len;
    size_t remaining = bufsize;

    len = snprintf(buf, remaining, "device:" CONFIG_ADBD_DEVICE_ID ":");

    if (len >= remaining) {
        return -1;
    }

#ifdef CONFIG_ADBD_PRODUCT_NAME
    remaining -= len;
    buf += len;
    len = snprintf(buf, remaining,
                   "ro.product.name=" CONFIG_ADBD_PRODUCT_NAME ";");

    if (len >= remaining) {
        return bufsize;
    }
#endif


#ifdef CONFIG_ADBD_PRODUCT_MODEL
    remaining -= len;
    buf += len;
    len = snprintf(buf, remaining,
                   "ro.product.model=" CONFIG_ADBD_PRODUCT_MODEL ";");

    if (len >= remaining) {
        return bufsize;
    }
#endif

#ifdef CONFIG_ADBD_PRODUCT_DEVICE
    remaining -= len;
    buf += len;
    len = snprintf(buf, remaining,
                   "ro.product.device=" CONFIG_ADBD_PRODUCT_DEVICE ";");

    if (len >= remaining) {
        return bufsize;
    }
#endif

    remaining -= len;
    buf += len;
    len = snprintf(buf, remaining, "features=" CONFIG_ADBD_FEATURES);

    if (len >= remaining) {
        return bufsize;
    }

    return bufsize - remaining + len;
}

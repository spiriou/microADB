/*
 * Copyright (C) 2020 Simon Piriou. All rights reserved.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 */

#include "adb.h"

int adb_fill_connect_data(char *buf, size_t bufsize)
{
    size_t len;
    size_t remaining = bufsize;

    len = snprintf(buf, remaining, "device:" CONFIG_SYSTEM_ADB_DEVICE_ID ":");

    if (len >= remaining) {
        return -1;
    }

#ifdef CONFIG_SYSTEM_ADB_PRODUCT_NAME
    remaining -= len;
    buf += len;
    len = snprintf(buf, remaining,
                   "ro.product.name=" CONFIG_SYSTEM_ADB_PRODUCT_NAME ";");

    if (len >= remaining) {
        return bufsize;
    }
#endif

#if 0
#ifdef CONFIG_SYSTEM_ADB_PRODUCT_MODEL
    remaining -= len;
    buf += len;
    len = snprintf(buf, remaining,
                   "ro.product.model=" CONFIG_SYSTEM_ADB_PRODUCT_MODEL ";");

    if (len >= remaining) {
        return bufsize;
    }
#endif

#ifdef CONFIG_SYSTEM_ADB_PRODUCT_DEVICE
    remaining -= len;
    buf += len;
    len = snprintf(buf, remaining,
                   "ro.product.device=" CONFIG_SYSTEM_ADB_PRODUCT_DEVICE ";");

    if (len >= remaining) {
        return bufsize;
    }
#endif

    len = snprintf(buf, remaining, "features=" CONFIG_SYSTEM_ADB_FEATURES);
                   // "features=cmd,shell_v2");

    if (len >= remaining) {
        return bufsize;
    }
#endif
    return bufsize - remaining + len;
}

int main(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);

    adb_context_t* ctx;

    ctx = adb_hal_create_context();
    if (!ctx) {
        return -1;
    }
    adb_hal_run(ctx);
    adb_hal_destroy_context(ctx);
    return 0;
}

/*
 * Copyright (C) 2022 Simon Piriou. All rights reserved.
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

#ifndef __TCP_SERVICE_H__
#define __TCP_SERVICE_H__

#include "adb.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

adb_service_t* tcp_forward_service(adb_client_t *client, const char *params,
                                   apacket *p);
adb_service_t* tcp_reverse_service(adb_client_t *client, const char *params,
                                   apacket *p);

adb_tcp_socket_t* tcp_allocate_rstream_socket(adb_client_t *client);
void tcp_release_rstream_socket(adb_tcp_socket_t *socket);

#endif /* __TCP_SERVICE_H__ */

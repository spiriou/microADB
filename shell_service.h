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

#ifndef _SHELL_SERVICE_H_
#define _SHELL_SERVICE_H_

#include "adb.h"

/****************************************************************************
 * Public types
 ****************************************************************************/

#define ADB_SHELL_PREFIX "shell:"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

adb_service_t* shell_service(adb_client_t *client, const char *params);

#endif /* _SHELL_SERVICE_H_ */

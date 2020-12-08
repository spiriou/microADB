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

#include <stdarg.h>
#include "adb.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void adb_log_impl(const char *func, int line, const char *fmt, ...) {
  va_list ap;
  printf("%s (%d): ", func, line);

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

void adb_reboot_impl(const char *target) {
    adb_log("reboot requested: <%s>\n", target);
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

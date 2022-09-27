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

#include "adb_list.h"

void adb_list_remove(adb_list_entry_t *head, adb_list_entry_t *entry)
{
    adb_list_entry_t *current = head->next;
    if (head->next == NULL) {
        /* List empty */
        return;
    }

    if (current == entry) {
        head->next = entry->next;
        return;
    }

    while (current->next) {
        if (current->next == entry) {
            current->next = entry->next;
            return;
        }
    }

    /* Entry not found */
}

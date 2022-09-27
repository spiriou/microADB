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

#ifndef __ADB_LIST_H
#define __ADB_LIST_H

#include <stddef.h>

typedef struct adb_list_entry_s {
    struct adb_list_entry_s *next;
} adb_list_entry_t;

#define adb_list_init(entry) (entry)->next = NULL
#define adb_list_empty(head) ((head)->next == NULL)
#define adb_list_next(entry) (entry)->next

#define adb_list_foreach(head, entry, type, member) \
  for((entry) = container_of((head)->next, type, member);\
      (entry); \
      (entry) = container_of((entry)->member.next, type, member))


static inline void adb_list_insert(adb_list_entry_t *head,
                                   adb_list_entry_t *entry) {
    entry->next = head->next;
    head->next = entry;
}

void adb_list_remove(adb_list_entry_t *head, adb_list_entry_t *entry);

#endif

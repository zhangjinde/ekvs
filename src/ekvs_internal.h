/* -*- Mode: C; tab-width: 3; c-basic-offset: 3; indent-tabs-mode: nil -*- */
/* vim: set filetype=C tabstop=3 softtabstop=3 shiftwidth=3 expandtab: */

/* ekvs -- Copyright (C) 2011 GameClay LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ekvs/ekvs.h>
#include <stdio.h>

struct _ekvs_db_entry {
   char flags;
   size_t key_sz;
   size_t data_sz;
   char key_data[1];
};

struct _ekvs_db {
   int last_error;
   FILE* db_file;
   struct _ekvs_db_entry* table;
   
   struct {
      uint64_t table_sz;
      size_t binlog_start;
   } serialized;
};

struct _kevs_binlog_entry {
   char operation;
   char flags;
   size_t key_sz;
   size_t data_sz;
   char key_data[1];
};

/* From lookup3 */
extern void hashlittle2( 
  const void *key,       /* the key to hash */
  size_t      length,    /* length of the key */
  uint32_t   *pc,        /* IN: primary initval, OUT: primary hash */
  uint32_t   *pb);       /* IN: secondary initval, OUT: secondary hash */

#define EKVS_BINLOG_SET 0
#define EKVS_BINLOG_DEL 1
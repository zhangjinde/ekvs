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

#include "ekvs_internal.h"

ekvs_malloc_ptr ekvs_malloc = malloc;
ekvs_realloc_ptr ekvs_realloc = realloc;
ekvs_free_ptr ekvs_free = free;

int ekvs_open(ekvs* store, const char* path, const ekvs_opts* opts)
{
   /* Check for user-specified allocators */
   if(opts->user_malloc != NULL || opts->user_realloc != NULL || opts->user_free != NULL)
   {
      if(opts->user_malloc == NULL || opts->user_realloc == NULL || opts->user_free == NULL)
      {
         fprintf(stderr, "ekvs: Specifying one user-allocation function requires specifying all allocation functions.\n");
         return EKVS_FAIL;
      }
      ekvs_malloc = opts->user_malloc;
      ekvs_realloc = opts->user_realloc;
      ekvs_free = opts->user_free;
   }

   *store = ekvs_malloc(sizeof(struct _ekvs_db));
   if(*store == NULL)
   {
      return EKVS_ALLOCATION_FAIL;
   }
   else
   {
      /* If a cache file is specified, open it */
      ekvs db = *store;
      FILE* dbfile = NULL;
      int file_created = 0;
      if(path != NULL)
      {
         FILE* dbfile = fopen(path, "rb+");
         if(dbfile == NULL)
         {
            file_created = 1;
            dbfile = fopen(path, "wb+");

            if(dbfile == NULL)
            {
               ekvs_free(*store);
               return EKVS_FILE_FAIL;
            }
         }
      }
      db->db_file = dbfile;

      /* Read in the serialized attributes of the db */
      fseek(dbfile, 0, SEEK_SET);
      if(dbfile != NULL && file_created == 0)
      {
         fread(&db->serialized, sizeof(db->serialized), 1, dbfile);
      }
      else
      {
         /* Set the initial table size */
         if(opts == NULL || opts->initial_table_size == 0)
         {
            db->serialized.table_sz = EKVS_INITIAL_TABLE_SIZE;
         }
         else
         {
            db->serialized.table_sz = opts->initial_table_size;
         }

         /* No binlog yet */
         db->serialized.binlog_start = sizeof(struct _ekvs_db_serialized);
         db->serialized.binlog_end = sizeof(struct _ekvs_db_serialized);

         /* Serialize initial DB settings */
         fwrite(&db->serialized, sizeof(db->serialized), 1, dbfile);
         fflush(dbfile);
      }

      /* Set up the hash-table */
      db->table = ekvs_malloc(sizeof(struct _ekvs_db_entry*) * db->serialized.table_sz);
      if(db->table == NULL)
      {
         fclose(dbfile);
         ekvs_free(*store);
         return EKVS_ALLOCATION_FAIL;
      }
      memset(db->table, 0, sizeof(struct _ekvs_db_entry*) * db->serialized.table_sz);

      /* Load up the table */
      {
         struct _ekvs_db_entry entry;
         struct _ekvs_db_entry* new_entry;
         long int binlog_start = db->serialized.binlog_start;
         long int binlog_end = db->serialized.binlog_end;
         long int filepos = ftell(dbfile);
         uint64_t hash;
         uint32_t pc, pb;
         char operation;

         /* Read the snapshot */
         while((binlog_start == binlog_end || filepos < binlog_start) && !feof(dbfile))
         {
            fread(&entry, sizeof(struct _ekvs_db_entry) - 1, 1, dbfile);

            /* Allocate enough space for the entry.
               The char[1] provides the space for NULL terminator on the key */
            new_entry = ekvs_malloc(sizeof(struct _ekvs_db_entry) + entry.key_sz + entry.data_sz);
            memcpy(new_entry, &entry, sizeof(struct _ekvs_db_entry));
            fread(&new_entry->key_data, 1, entry.key_sz, dbfile);
            new_entry->key_data[entry.key_sz] = '\0';
            fread(&new_entry->key_data + entry.key_sz + 1, 1, entry.data_sz, dbfile);
            filepos = ftell(dbfile);

            /* Assign to table */
            hashlittle2(new_entry->key_data, entry.key_sz, &pc, &pb);
            hash = pc + (((uint64_t)pb) << 32);
            (*store)->table[hash % (*store)->serialized.table_sz] = new_entry;
         }

         /* Read/replay the binlog */
         fseek(dbfile, binlog_start, SEEK_SET);
         new_entry = ekvs_malloc(sizeof(struct _ekvs_db_entry));
         while(binlog_start != binlog_end && filepos < binlog_end)
         {
            fread(&operation, sizeof(operation), 1, dbfile);
            fread(&entry, sizeof(struct _ekvs_db_entry) - 1, 1, dbfile);

            new_entry = ekvs_realloc(new_entry, sizeof(struct _ekvs_db_entry) + entry.key_sz + entry.data_sz);
            memcpy(new_entry, &entry, sizeof(struct _ekvs_db_entry));
            fread(&new_entry->key_data, 1, entry.key_sz, dbfile);
            new_entry->key_data[entry.key_sz] = '\0';
            fread(&new_entry->key_data + entry.key_sz + 1, 1, entry.data_sz, dbfile);
            filepos = ftell(dbfile);

            /* Replay */
            (*store)->last_error = _ekvs_replay_binlog_entry(*store, operation, new_entry);
            if((*store)->last_error != EKVS_OK)
            {
               fprintf(stderr, "Error replaying binlog.");
               break;
            }
         }
         ekvs_free(new_entry);
      }

      (*store)->last_error = EKVS_OK;
   }
   
   return (*store)->last_error;
}

void ekvs_close(ekvs store)
{
   if(store != NULL)
   {
      uint64_t i;
      for(i = 0; i < store->serialized.table_sz; i++)
      {
         if(store->table[i] != NULL) ekvs_free(store->table[i]);
      }
      ekvs_free(store->table);
      fclose(store->db_file);
      ekvs_free(store);
   }
}

int ekvs_last_error(ekvs store)
{
   return store->last_error;
}

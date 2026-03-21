/**
 * Copyright (C) 2026 bytetilde
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "commonapi.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
static inline unsigned long long hash(const char* str) {
  // djb2
  unsigned long long hash = 5381;
  int c;
  while((c = *str++)) hash = ((hash << 5) + hash) + c;
  return hash;
}

// hashmap implementation
typedef struct hashmap_entry_t {
  unsigned long long key;
  void* value;
  bool occupied;
} hashmap_entry_t;
typedef struct hashmap_t {
  hashmap_entry_t* buckets;
  size_t capacity;
  size_t size;
} hashmap_t;
static inline hashmap_t* hm_create(size_t capacity) {
  if(capacity < 4) capacity = 4;
  hashmap_t* map = malloc(sizeof(hashmap_t));
  map->capacity = capacity;
  map->size = 0;
  map->buckets = calloc(capacity, sizeof(hashmap_entry_t));
  return map;
}
static void hm_rehash(hashmap_t* map, size_t new_capacity) {
  if(new_capacity < 4) new_capacity = 4;
  hashmap_entry_t* old_buckets = map->buckets;
  size_t old_capacity = map->capacity;
  map->buckets = calloc(new_capacity, sizeof(hashmap_entry_t));
  map->capacity = new_capacity;
  map->size = 0;
  for(size_t i = 0; i < old_capacity; ++i) {
    if(old_buckets[i].occupied) {
      size_t idx = (size_t)(old_buckets[i].key % map->capacity);
      while(map->buckets[idx].occupied) idx = (idx + 1) % map->capacity;
      map->buckets[idx].key = old_buckets[i].key;
      map->buckets[idx].value = old_buckets[i].value;
      map->buckets[idx].occupied = true;
      ++map->size;
    }
  }
  free(old_buckets);
}
static inline void hm_insert(hashmap_t* map, unsigned long long key, void* value) {
  if(map->size >= map->capacity / 2) hm_rehash(map, map->capacity * 2);
  size_t idx = (size_t)(key % map->capacity);
  while(map->buckets[idx].occupied) {
    if(map->buckets[idx].key == key) {
      map->buckets[idx].value = value;
      return;
    }
    idx = (idx + 1) % map->capacity;
  }
  map->buckets[idx].key = key;
  map->buckets[idx].value = value;
  map->buckets[idx].occupied = true;
  ++map->size;
}
static inline void hm_remove(hashmap_t* map, unsigned long long key) {
  size_t idx = (size_t)(key % map->capacity);
  while(map->buckets[idx].occupied) {
    if(map->buckets[idx].key == key) {
      map->buckets[idx].occupied = false;
      --map->size;
      size_t i = (idx + 1) % map->capacity;
      while(map->buckets[i].occupied) {
        unsigned long long k = map->buckets[i].key;
        void* v = map->buckets[i].value;
        map->buckets[i].occupied = false;
        --map->size;
        hm_insert(map, k, v);
        i = (i + 1) % map->capacity;
      }
      if(map->capacity > 16 && map->size > 0 && map->size <= map->capacity / 8)
        hm_rehash(map, map->capacity / 2);
      return;
    }
    idx = (idx + 1) % map->capacity;
  }
}
static inline void* hm_get(hashmap_t* map, unsigned long long key) {
  if(!map->buckets) return NULL;
  size_t idx = (size_t)(key % map->capacity);
  size_t start_idx = idx;
  while(map->buckets[idx].occupied) {
    if(map->buckets[idx].key == key) return map->buckets[idx].value;
    idx = (idx + 1) % map->capacity;
    if(idx == start_idx) break;
  }
  return NULL;
}
static inline void hm_destroy(hashmap_t* map) {
  free(map->buckets);
  free(map);
}

typedef struct hook_payload_t {
  desktop_t* desktop;
  window_t* window;
  void* data;
} hook_payload_t;
typedef void* (*hook_func_t)(hook_payload_t* payload);
typedef struct hook_t {
  unsigned long long name;
  hook_func_t function;
  struct hook_t* next;
  // linked list because i couldnt be bothered
} hook_t;
typedef struct hookman_t {
  unsigned long long magic;
  pthread_mutex_t lock;
  hashmap_t* hooks;
  hashmap_t* hooks_before;
  hashmap_t* hooks_after;
  hashmap_t* exports;
  bool (*orig_desktop_update)(desktop_t* desktop);
  void (*orig_desktop_draw)(desktop_t* desktop);
  bool (*orig_dispatch_window_event)(desktop_t* desktop, window_t* window, int event, void* data);
} hookman_t;
#define HOOKMAN_MAGIC 0x1ae71de1ac6913ULL

static inline hookman_t* hookman_find(desktop_t* desktop) {
  // big big bandaid that assumes the function is called only in window_init
  for(int i = 0; i <= desktop->window_count; ++i) {
    window_t* win = desktop->windows + i;
    if(win->data && ((hookman_t*)win->data)->magic == HOOKMAN_MAGIC) return (hookman_t*)win->data;
  }
  return NULL;
}
static inline void hookman_attach_to(hashmap_t* map, pthread_mutex_t* lock, const char* name,
                                     const char* hook_point, hook_func_t callback) {
  unsigned long long point_hash = hash(hook_point);
  unsigned long long name_hash = hash(name);
  pthread_mutex_lock(lock);
  hook_t* list = hm_get(map, point_hash);
  hook_t* new_hook = malloc(sizeof(hook_t));
  new_hook->name = name_hash;
  new_hook->function = callback;
  new_hook->next = list;
  hm_insert(map, point_hash, new_hook);
  pthread_mutex_unlock(lock);
}
static inline void hookman_attach(hookman_t* hm, const char* name, const char* hook_point,
                                  hook_func_t callback) {
  if(!hm) return;
  hookman_attach_to(hm->hooks, &hm->lock, name, hook_point, callback);
}
static inline void hookman_attach_before(hookman_t* hm, const char* name, const char* hook_point,
                                         hook_func_t callback) {
  if(!hm) return;
  hookman_attach_to(hm->hooks_before, &hm->lock, name, hook_point, callback);
}
static inline void hookman_attach_after(hookman_t* hm, const char* name, const char* hook_point,
                                        hook_func_t callback) {
  if(!hm) return;
  hookman_attach_to(hm->hooks_after, &hm->lock, name, hook_point, callback);
}
static inline void hookman_detach_all(hookman_t* hm, const char* name) {
  if(!hm) return;
  unsigned long long name_hash = hash(name);
  pthread_mutex_lock(&hm->lock);
  hashmap_t* maps[] = {hm->hooks, hm->hooks_before, hm->hooks_after};
  for(int m = 0; m < 3; ++m) {
    hashmap_t* map = maps[m];
    for(size_t i = 0; i < map->capacity; ++i) {
      if(!map->buckets[i].occupied) continue;
      hook_t* prev = NULL;
      hook_t* curr = map->buckets[i].value;
      while(curr) {
        if(curr->name == name_hash) {
          hook_t* next = curr->next;
          if(prev) prev->next = next;
          else map->buckets[i].value = next;
          free(curr);
          curr = next;
        } else {
          prev = curr;
          curr = curr->next;
        }
      }
    }
  }
  pthread_mutex_unlock(&hm->lock);
}

static inline void hookman_export(hookman_t* hm, const char* name, void* func) {
  if(!hm) return;
  unsigned long long key = hash(name);
  pthread_mutex_lock(&hm->lock);
  hm_insert(hm->exports, key, func);
  pthread_mutex_unlock(&hm->lock);
}
static inline void hookman_unexport(hookman_t* hm, const char* name) {
  if(!hm) return;
  unsigned long long key = hash(name);
  pthread_mutex_lock(&hm->lock);
  hm_remove(hm->exports, key);
  pthread_mutex_unlock(&hm->lock);
}
static inline bool hookman_is_exported(hookman_t* hm, const char* name) {
  if(!hm) return false;
  unsigned long long key = hash(name);
  pthread_mutex_lock(&hm->lock);
  bool exported = hm_get(hm->exports, key) != NULL;
  pthread_mutex_unlock(&hm->lock);
  return exported;
}
static inline void* hookman_call(hookman_t* hm, const char* name, void* data) {
  if(!hm) return NULL;
  unsigned long long key = hash(name);
  pthread_mutex_lock(&hm->lock);
  void* func = hm_get(hm->exports, key);
  pthread_mutex_unlock(&hm->lock);
  if(!func) return NULL;
  return ((void* (*)(void*))func)(data);
}

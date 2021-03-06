/*
 * map.h
 *
 *  Created on: Jul 7, 2016
 *      Author: Jeff
 */

#ifndef MAP_H_
#define MAP_H_

#include <stdbool.h>
#include <stdint.h>

#include "../error.h"
#include "../shared.h"

#define DEFAULT_MAP_SZ 31

#ifdef DEBUG
extern int MAP__count;
extern int MAP__insert_count;
extern int MAP__insert_compares_count;
extern int MAP__lookup_count;
extern int MAP__lookup_compares_count;
#endif

typedef struct Pair_ {
  const void *key;
  void *value;
} Pair;

typedef struct MEntry_ MEntry;
struct MEntry_ {
  Pair pair;
  uint32_t hash_value;
  int32_t num_probes;
  MEntry *prev, *next;
};

typedef struct Map_ {
  Hasher hash;
  Comparator compare;
  uint32_t table_sz, num_entries, entries_thresh;
  MEntry *table, *first, *last;
} Map;

typedef void (*PairAction)(Pair *kv);

Map *map_create(uint32_t size, Hasher, Comparator);
void map_init(Map *map, uint32_t size, Hasher, Comparator);

void map_init_default(Map *map);
Map *map_create_default();

void map_finalize(Map *);
void map_delete(Map *);

bool map_insert(Map *, const void *key, const void *value);
Pair map_remove(Map *, const void *key);

DEB_FN(void *, map_lookup, const Map *map, const void *key);
#define map_lookup(...) CALL_FN(map_lookup__, __VA_ARGS__)

DEB_FN(void, map_iterate, const Map *, PairAction);
#define map_iterate(...) CALL_FN(map_iterate__, __VA_ARGS__)

uint32_t map_size(const Map *);

#endif /* MAP_H_ */

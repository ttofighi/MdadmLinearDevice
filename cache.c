#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
  if (num_entries < 2 || num_entries > 4096 || cache != NULL){ //test cases
    return -1;
  }
  else{
    cache = calloc(num_entries, sizeof(cache_entry_t)); //dynamically allocate number of entries to the cache
    cache_size = num_entries;

    //reset number of queries and hits when we create cache
    num_queries = 0;
    num_hits = 0;
    return 1;
  }
}

int cache_destroy(void) {
  if (cache == NULL || cache_size == 0){ //test cases
    return -1;
  }
  free(cache); //deallocate cache
  cache_size = 0;
  cache = NULL;
  return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  if (cache_size == 0 || buf == NULL || cache == NULL){ //invalid parameters
    return -1;
  }
  if (disk_num < 0 || disk_num > 15 || block_num < 0 || block_num > 255){ //invalid parameters
    return -1;
  }

  num_queries++;
  for(int i = 0; i < cache_size; i++){
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num){ //see if disk number and block number can be accessed
      if(cache[i].valid == true){
        num_hits++;
        cache[i].num_accesses += 1;
        memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE); //copy block and disk info into the buffer
        return 1;
      }
    }
  }
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {

  if (cache_size == 0 || buf == NULL || cache == NULL){ //invalid parameters
    return;
  }
  else if (disk_num < 0 || disk_num > 15 || block_num < 0 || block_num > 255){ //invalid parameters
    return;
  }
  
  //increment number of accesses if disk and block are in cache
  for(int i = 0; i < cache_size; i++){ 
    if (cache[i].valid == true){
      if(cache[i].disk_num == disk_num && cache[i].block_num == block_num){
        cache[i].num_accesses += 1;
        memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      }
    }
  }
  return;
}

//check num_accesses parameter
//find lowest among all num_accesses entries
//once found, insert cache into that location
int lfu(){
  int min = cache[0].num_accesses;
  int j = 0;

  for(int i = 1; i < cache_size; i++){
    if (cache[i].num_accesses < min){
      min = cache[i].num_accesses;
      j = i;
    }
  }

  return j;
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {

  if (buf == NULL || cache_size == 0 || cache == NULL){ //invalid parameters
    return -1; 
  }
  else if (disk_num < 0 || disk_num > 15 || block_num < 0 || block_num > 255){ //invalid parameters
    return -1;
  }

  //if cache already exists, don't do anything
  for (int i=0; i < cache_size; i++){
    if (cache[i].valid == true){
      if (cache[i].disk_num == disk_num && cache[i].block_num == block_num){
        if (memcmp(buf, cache[i].block, JBOD_BLOCK_SIZE) == 0){
          return -1;
        }
      }
    }
  }


  //if cache does not exist, insert block and disk numbers into cache
  for (int i=0; i < cache_size; i++){
    if (cache[i].valid == false){
        memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
        cache[i].num_accesses = 1;
        cache[i].block_num = block_num;
        cache[i].disk_num = disk_num;
        cache[i].valid = true;
        return 1;
      }
  }

  //replace cache if block and disk numbers already exist
  int insert = lfu();
  memcpy(cache[insert].block, buf, JBOD_BLOCK_SIZE);
  cache[insert].num_accesses = 1;
  cache[insert].valid = true;
  cache[insert].disk_num = disk_num;
  cache[insert].block_num = block_num;

  return 1;
 }

bool cache_enabled(void) {
	return cache != NULL && cache_size > 0;
}

void cache_print_hit_rate(void) {
	fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
	fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}

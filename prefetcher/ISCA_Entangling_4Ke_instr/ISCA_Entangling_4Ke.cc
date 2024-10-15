////////////////////////////////////////////////////////////////////////
//
//  Implementation for the Entangling Instruction Prefetcher
//  presented at ISCA'21.
//
//  Authors: Alberto Ros (aros@ditec.um.es)
//           Alexandra Jimborean (alexandra.jimborean@um.es)
//
//  Cite: Alberto Ros and Alexandra Jimborean, "A Cost-Effective
//        Entangling Prefetcher for Instructions," ISCA, june, 2021.
//
////////////////////////////////////////////////////////////////////////

#include <cassert>
#include <iostream>

#include "cache.h"

#define MAX_PQ_SIZE 32
#define MAX_RQ_SIZE 64
#define MAX_NUM_SET 64
#define MAX_NUM_WAY 8

using namespace std;

// To access cpu in my functions
uint32_t l1i_cpu_id;
uint64_t l1i_current_cycle;

uint64_t l1i_last_basic_block;
uint32_t l1i_consecutive_count;
uint32_t l1i_basic_block_merge_diff;

// SIZE
uint32_t l1i_history_table_size;
uint32_t l1i_timing_mshr_table_size;
uint32_t l1i_confidence_cache_table_size;
uint32_t l1i_entangled_table_size;
uint32_t l1i_xp_queue_size;

#define L1I_HIST_TABLE_ENTRIES 32 // 16

// LINE AND MERGE BASIC BLOCK SIZE

#define L1I_MERGE_BBSIZE_BITS 6
#define L1I_MERGE_BBSIZE_MAX_VALUE ((1 << L1I_MERGE_BBSIZE_BITS) - 1)

// TIME AND OVERFLOWS

#define L1I_TIME_DIFF_BITS 20
#define L1I_TIME_DIFF_OVERFLOW ((uint64_t)1 << L1I_TIME_DIFF_BITS)
#define L1I_TIME_DIFF_MASK (L1I_TIME_DIFF_OVERFLOW - 1)

#define L1I_TIME_BITS 12
#define L1I_TIME_OVERFLOW ((uint64_t)1 << L1I_TIME_BITS)
#define L1I_TIME_MASK (L1I_TIME_OVERFLOW - 1)

uint64_t l1i_get_latency(uint64_t cycle, uint64_t cycle_prev)
{
  uint64_t cycle_masked = cycle & L1I_TIME_MASK;
  uint64_t cycle_prev_masked = cycle_prev & L1I_TIME_MASK;
  if (cycle_prev_masked > cycle_masked) {
    return (cycle_masked + L1I_TIME_OVERFLOW) - cycle_prev_masked;
  }
  return cycle_masked - cycle_prev_masked;
}

// ENTANGLED COMPRESSION FORMAT

#define L1I_ENTANGLED_MAX_FORMATS 7

// STATS
#define L1I_STATS_TABLE_INDEX_BITS 16
#define L1I_STATS_TABLE_ENTRIES (1 << L1I_STATS_TABLE_INDEX_BITS)
#define L1I_STATS_TABLE_MASK (L1I_STATS_TABLE_ENTRIES - 1)

typedef struct __l1i_stats_entry {
  uint64_t accesses;
  uint64_t misses;
  uint64_t hits;
  uint64_t late;
  uint64_t wrong; // early
} l1i_stats_entry;

l1i_stats_entry l1i_stats_table[NUM_CPUS][L1I_STATS_TABLE_ENTRIES];
uint64_t l1i_stats_discarded_prefetches;
uint64_t l1i_stats_evict_entangled_j_table;
uint64_t l1i_stats_evict_entangled_k_table;
uint64_t l1i_stats_max_bb_size;
uint64_t l1i_stats_max_xpq_size;
uint64_t l1i_stats_formats[L1I_ENTANGLED_MAX_FORMATS];
uint64_t l1i_stats_cycle_operate;
uint64_t l1i_stats_cycle_no_operate;
uint64_t l1i_stats_last_cycle_operate[NUM_CPUS];
uint64_t l1i_stats_hist_lookups[L1I_HIST_TABLE_ENTRIES + 2];
uint64_t l1i_stats_basic_blocks[L1I_MERGE_BBSIZE_MAX_VALUE + 1];
uint64_t l1i_stats_entangled[L1I_ENTANGLED_MAX_FORMATS + 1];
uint64_t l1i_stats_basic_blocks_ent[L1I_MERGE_BBSIZE_MAX_VALUE + 1];

void l1i_init_stats_table()
{
  for (int i = 0; i < L1I_STATS_TABLE_ENTRIES; i++) {
    l1i_stats_table[l1i_cpu_id][i].accesses = 0;
    l1i_stats_table[l1i_cpu_id][i].misses = 0;
    l1i_stats_table[l1i_cpu_id][i].hits = 0;
    l1i_stats_table[l1i_cpu_id][i].late = 0;
    l1i_stats_table[l1i_cpu_id][i].wrong = 0;
  }
  l1i_stats_discarded_prefetches = 0;
  l1i_stats_evict_entangled_j_table = 0;
  l1i_stats_evict_entangled_k_table = 0;
  l1i_stats_max_bb_size = 0;
  l1i_stats_max_xpq_size = 0;
  for (int i = 0; i < L1I_ENTANGLED_MAX_FORMATS; i++) {
    l1i_stats_formats[i] = 0;
  }
  l1i_stats_cycle_operate = 0;
  l1i_stats_cycle_no_operate = 0;
  for (uint32_t i = 0; i < NUM_CPUS; i++) {
    l1i_stats_last_cycle_operate[i] = l1i_current_cycle;
  }
  for (int i = 0; i <= L1I_HIST_TABLE_ENTRIES; i++) {
    l1i_stats_hist_lookups[i] = 0;
  }
  for (int i = 0; i <= L1I_MERGE_BBSIZE_MAX_VALUE; i++) {
    l1i_stats_basic_blocks[i] = 0;
  }
  for (int i = 0; i <= L1I_ENTANGLED_MAX_FORMATS; i++) {
    l1i_stats_entangled[i] = 0;
  }
  for (int i = 0; i <= L1I_MERGE_BBSIZE_MAX_VALUE; i++) {
    l1i_stats_basic_blocks_ent[i] = 0;
  }
}

void l1i_print_stats_table()
{
  std::cout << "IP accesses: ";
  uint64_t max = 0;
  uint64_t max_addr = 0;
  uint64_t total_accesses = 0;
  for (uint32_t i = 0; i < L1I_STATS_TABLE_ENTRIES; i++) {
    if (l1i_stats_table[l1i_cpu_id][i].accesses > max) {
      max = l1i_stats_table[l1i_cpu_id][i].accesses;
      max_addr = i;
    }
    total_accesses += l1i_stats_table[l1i_cpu_id][i].accesses;
  }
  std::cout << std::hex << max_addr << " " << (max_addr << LOG2_BLOCK_SIZE) << std::dec << " " << max << " / " << total_accesses << std::endl;
  std::cout << "IP misses: ";
  max = 0;
  max_addr = 0;
  uint64_t total_misses = 0;
  for (uint32_t i = 0; i < L1I_STATS_TABLE_ENTRIES; i++) {
    if (l1i_stats_table[l1i_cpu_id][i].misses > max) {
      max = l1i_stats_table[l1i_cpu_id][i].misses;
      max_addr = i;
    }
    total_misses += l1i_stats_table[l1i_cpu_id][i].misses;
  }
  std::cout << std::hex << max_addr << " " << (max_addr << LOG2_BLOCK_SIZE) << std::dec << " " << max << " / " << total_misses << std::endl;
  std::cout << "IP hits: ";
  max = 0;
  max_addr = 0;
  uint64_t total_hits = 0;
  for (uint32_t i = 0; i < L1I_STATS_TABLE_ENTRIES; i++) {
    if (l1i_stats_table[l1i_cpu_id][i].hits > max) {
      max = l1i_stats_table[l1i_cpu_id][i].hits;
      max_addr = i;
    }
    total_hits += l1i_stats_table[l1i_cpu_id][i].hits;
  }
  std::cout << std::hex << max_addr << " " << (max_addr << LOG2_BLOCK_SIZE) << std::dec << " " << max << " / " << total_hits << std::endl;
  std::cout << "IP late: ";
  max = 0;
  max_addr = 0;
  uint64_t total_late = 0;
  for (uint32_t i = 0; i < L1I_STATS_TABLE_ENTRIES; i++) {
    if (l1i_stats_table[l1i_cpu_id][i].late > max) {
      max = l1i_stats_table[l1i_cpu_id][i].late;
      max_addr = i;
    }
    total_late += l1i_stats_table[l1i_cpu_id][i].late;
  }
  std::cout << std::hex << max_addr << " " << (max_addr << LOG2_BLOCK_SIZE) << std::dec << " " << max << " / " << total_late << std::endl;
  std::cout << "IP wrong: ";
  max = 0;
  max_addr = 0;
  uint64_t total_wrong = 0;
  for (uint32_t i = 0; i < L1I_STATS_TABLE_ENTRIES; i++) {
    if (l1i_stats_table[l1i_cpu_id][i].wrong > max) {
      max = l1i_stats_table[l1i_cpu_id][i].wrong;
      max_addr = i;
    }
    total_wrong += l1i_stats_table[l1i_cpu_id][i].wrong;
  }
  std::cout << std::hex << max_addr << " " << (max_addr << LOG2_BLOCK_SIZE) << std::dec << " " << max << " / " << total_wrong << std::endl;

  std::cout << "miss rate: " << ((double)total_misses / (double)total_accesses) << std::endl;
  std::cout << "coverage: " << ((double)total_hits / (double)(total_hits + total_misses)) << std::endl;
  std::cout << "coverage_late: " << ((double)(total_hits + total_late) / (double)(total_hits + total_misses)) << std::endl;
  std::cout << "accuracy: " << ((double)total_hits / (double)(total_hits + total_late + total_wrong)) << std::endl;
  std::cout << "accuracy_late: " << ((double)(total_hits + total_late) / (double)(total_hits + total_late + total_wrong)) << std::endl;
  std::cout << "discarded: " << l1i_stats_discarded_prefetches << std::endl;
  std::cout << "evicts entangled j table: " << l1i_stats_evict_entangled_j_table << std::endl;
  std::cout << "evicts entangled k table: " << l1i_stats_evict_entangled_k_table << std::endl;
  std::cout << "max bb size: " << l1i_stats_max_bb_size << std::endl;
  std::cout << "max xpq size: " << l1i_stats_max_xpq_size << std::endl;
  std::cout << "formats: ";
  for (uint32_t i = 0; i < L1I_ENTANGLED_MAX_FORMATS; i++) {
    std::cout << l1i_stats_formats[i] << " ";
  }
  std::cout << std::endl;
  std::cout << "history table size: " << l1i_history_table_size << " bytes; " << (double)l1i_history_table_size / 1024 << " KB" << std::endl;
  std::cout << "cycles_no_operate: " << (double)l1i_stats_cycle_no_operate * 100 / (double)(l1i_stats_cycle_no_operate + l1i_stats_cycle_operate) << " %"
            << std::endl;
  std::cout << "hist_lookups: ";
  uint64_t total_hist_lookups = 0;
  for (uint32_t i = 0; i <= L1I_HIST_TABLE_ENTRIES + 1; i++) {
    std::cout << l1i_stats_hist_lookups[i] << " ";
    total_hist_lookups += l1i_stats_hist_lookups[i];
  }
  std::cout << std::endl;
  std::cout << "hist_lookups_evict: " << (double)l1i_stats_hist_lookups[L1I_HIST_TABLE_ENTRIES] * 100 / (double)(total_hist_lookups) << " %" << std::endl;
  std::cout << "hist_lookups_shortlat: " << (double)l1i_stats_hist_lookups[L1I_HIST_TABLE_ENTRIES + 1] * 100 / (double)(total_hist_lookups) << " %"
            << std::endl;

  std::cout << "bb_found_hist: ";
  uint64_t total_bb_found = 0;
  uint64_t total_bb_prefetches = 0;
  for (uint32_t i = 0; i <= L1I_MERGE_BBSIZE_MAX_VALUE; i++) {
    std::cout << l1i_stats_basic_blocks[i] << " ";
    total_bb_found += i * l1i_stats_basic_blocks[i];
    total_bb_prefetches += l1i_stats_basic_blocks[i];
  }
  std::cout << std::endl;
  std::cout << "bb_found_summary: " << total_bb_found << " " << total_bb_prefetches << " " << (double)total_bb_found / (double)total_bb_prefetches << std::endl;

  std::cout << "entangled_found_hist: ";
  uint64_t total_entangled_found = 0;
  uint64_t total_ent_prefetches = 0;
  for (uint32_t i = 0; i <= L1I_ENTANGLED_MAX_FORMATS; i++) {
    std::cout << l1i_stats_entangled[i] << " ";
    total_entangled_found += i * l1i_stats_entangled[i];
    total_ent_prefetches += l1i_stats_entangled[i];
  }
  std::cout << std::endl;
  std::cout << "entangled_found_summary: " << total_entangled_found << " " << total_ent_prefetches << " "
            << (double)total_entangled_found / (double)total_ent_prefetches << std::endl;

  std::cout << "bb_ent_found_hist: ";
  uint64_t total_bb_ent_found = 0;
  uint64_t total_bb_ent_prefetches = 0;
  for (uint32_t i = 0; i <= L1I_MERGE_BBSIZE_MAX_VALUE; i++) {
    std::cout << l1i_stats_basic_blocks_ent[i] << " ";
    total_bb_ent_found += i * l1i_stats_basic_blocks_ent[i];
    total_bb_ent_prefetches += l1i_stats_basic_blocks_ent[i];
  }
  std::cout << std::endl;
  std::cout << "bb_ent_found_summary: " << total_bb_ent_found << " " << total_bb_ent_prefetches << " "
            << (double)total_bb_ent_found / (double)total_bb_ent_prefetches << std::endl;
}

// HISTORY TABLE

#define L1I_HIST_TABLE_MASK (L1I_HIST_TABLE_ENTRIES - 1)
#define L1I_BB_MERGE_ENTRIES 6
#define L1I_HIST_TAG_BITS 58
#define L1I_HIST_TAG_MASK (((uint64_t)1 << L1I_HIST_TAG_BITS) - 1)

typedef struct __l1i_hist_entry {
  uint64_t tag;       // L1I_HIST_TAG_BITS bits
  uint64_t time_diff; // L1I_TIME_DIFF_BITS bits
  uint32_t bb_size;   // L1I_MERGE_BBSIZE_BITS bits
} l1i_hist_entry;

l1i_hist_entry l1i_hist_table[NUM_CPUS][L1I_HIST_TABLE_ENTRIES];
uint64_t l1i_hist_table_head[NUM_CPUS];      // log_2 (L1I_HIST_TABLE_ENTRIES)
uint64_t l1i_hist_table_head_time[NUM_CPUS]; // 64 bits

void l1i_init_hist_table()
{
  l1i_hist_table_head[l1i_cpu_id] = 0;
  l1i_hist_table_head_time[l1i_cpu_id] = l1i_current_cycle;
  for (uint32_t i = 0; i < L1I_HIST_TABLE_ENTRIES; i++) {
    l1i_hist_table[l1i_cpu_id][i].tag = 0;
    l1i_hist_table[l1i_cpu_id][i].time_diff = 0;
    l1i_hist_table[l1i_cpu_id][i].bb_size = 0;
  }
}

uint64_t l1i_find_hist_entry(uint64_t line_addr)
{
  uint64_t tag = line_addr & L1I_HIST_TAG_MASK;
  for (uint32_t count = 0, i = (l1i_hist_table_head[l1i_cpu_id] + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES; count < L1I_HIST_TABLE_ENTRIES;
       count++, i = (i + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES) {
    if (l1i_hist_table[l1i_cpu_id][i].tag == tag)
      return i;
  }
  return L1I_HIST_TABLE_ENTRIES;
}

// It can have duplicated entries if the line was evicted in between
uint32_t l1i_add_hist_table(uint64_t line_addr)
{
  // Insert empty addresses in hist not to have timediff overflows
  while (l1i_current_cycle - l1i_hist_table_head_time[l1i_cpu_id] >= L1I_TIME_DIFF_OVERFLOW) {
    l1i_hist_table[l1i_cpu_id][l1i_hist_table_head[l1i_cpu_id]].tag = 0;
    l1i_hist_table[l1i_cpu_id][l1i_hist_table_head[l1i_cpu_id]].time_diff = L1I_TIME_DIFF_MASK;
    l1i_hist_table[l1i_cpu_id][l1i_hist_table_head[l1i_cpu_id]].bb_size = 0;
    l1i_hist_table_head[l1i_cpu_id] = (l1i_hist_table_head[l1i_cpu_id] + 1) % L1I_HIST_TABLE_ENTRIES;
    l1i_hist_table_head_time[l1i_cpu_id] += L1I_TIME_DIFF_MASK;
  }

  // Allocate a new entry (evict old one if necessary)
  l1i_hist_table[l1i_cpu_id][l1i_hist_table_head[l1i_cpu_id]].tag = line_addr & L1I_HIST_TAG_MASK;
  l1i_hist_table[l1i_cpu_id][l1i_hist_table_head[l1i_cpu_id]].time_diff = (l1i_current_cycle - l1i_hist_table_head_time[l1i_cpu_id]) & L1I_TIME_DIFF_MASK;
  l1i_hist_table[l1i_cpu_id][l1i_hist_table_head[l1i_cpu_id]].bb_size = 0;
  uint32_t pos = l1i_hist_table_head[l1i_cpu_id];
  l1i_hist_table_head[l1i_cpu_id] = (l1i_hist_table_head[l1i_cpu_id] + 1) % L1I_HIST_TABLE_ENTRIES;
  l1i_hist_table_head_time[l1i_cpu_id] = l1i_current_cycle;
  return pos;
}

void l1i_add_bb_size_hist_table(uint64_t line_addr, uint32_t bb_size)
{
  uint64_t index = l1i_find_hist_entry(line_addr);
  l1i_hist_table[l1i_cpu_id][index].bb_size = bb_size & L1I_MERGE_BBSIZE_MAX_VALUE;
}

uint32_t l1i_find_bb_merge_hist_table(uint64_t line_addr)
{
  uint64_t tag = line_addr & L1I_HIST_TAG_MASK;
  for (uint32_t count = 0, i = (l1i_hist_table_head[l1i_cpu_id] + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES; count < L1I_HIST_TABLE_ENTRIES;
       count++, i = (i + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES) {
    if (count >= L1I_BB_MERGE_ENTRIES) {
      return 0;
    }
    if (tag > l1i_hist_table[l1i_cpu_id][i].tag && (tag - l1i_hist_table[l1i_cpu_id][i].tag) <= l1i_hist_table[l1i_cpu_id][i].bb_size) { // try: bb_size + 1
      //&& (tag - l1i_hist_table[l1i_cpu_id][i].tag) == l1i_hist_table[l1i_cpu_id][i].bb_size) {
      return tag - l1i_hist_table[l1i_cpu_id][i].tag;
    }
  }
  assert(false);
}

// return bere (best request -- entangled address)
uint64_t l1i_get_bere_hist_table(uint64_t line_addr, uint32_t pos_hist, uint64_t latency, uint32_t skip = 0)
{
  assert(pos_hist < L1I_HIST_TABLE_ENTRIES);
  uint64_t tag = line_addr & L1I_HIST_TAG_MASK;
  // assert(tag);
  if (!tag) {
    return -1;
  }
  if (l1i_hist_table[l1i_cpu_id][pos_hist].tag != tag) {
    l1i_stats_hist_lookups[L1I_HIST_TABLE_ENTRIES]++;
    return 0; // removed
  }
  uint32_t next_pos = (pos_hist + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES;
  uint32_t first = (l1i_hist_table_head[l1i_cpu_id] + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES;
  uint64_t time_i = l1i_hist_table[l1i_cpu_id][pos_hist].time_diff;
  uint32_t num_skipped = 0;
  for (uint32_t count = 0, i = next_pos; i != first; count++, i = (i + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES) {
    // Against the time overflow
    if (l1i_hist_table[l1i_cpu_id][i].tag == tag) {
      return 0; // Second time it appeared (it was evicted in between) or many for the same set. No entangle
    }
    if (l1i_hist_table[l1i_cpu_id][i].tag && time_i >= latency) {
      if (skip == num_skipped) {
        l1i_stats_hist_lookups[count]++;
        return l1i_hist_table[l1i_cpu_id][i].tag;
      } else {
        num_skipped++;
      }
    }
    time_i += l1i_hist_table[l1i_cpu_id][i].time_diff;
  }
  l1i_stats_hist_lookups[L1I_HIST_TABLE_ENTRIES + 1]++;
  return 0;
}

// TIMING TABLES

#define L1I_SET_BITS 6
#define L1I_TIMING_MSHR_SIZE (MAX_PQ_SIZE + MAX_RQ_SIZE + 1024)
#define L1I_TIMING_MSHR_TAG_BITS 42
#define L1I_TIMING_MSHR_TAG_MASK (((uint64_t)1 << L1I_HIST_TAG_BITS) - 1)
#define L1I_TIMING_CACHE_TAG_BITS (L1I_TIMING_MSHR_TAG_BITS - L1I_SET_BITS)
#define L1I_TIMING_CACHE_TAG_MASK (((uint64_t)1 << L1I_HIST_TAG_BITS) - 1)

#define L1I_ENTANGLED_TABLE_WAYS 16

// We do not have access to the MSHR, so we aproximate it using this structure
typedef struct __l1i_timing_mshr_entry {
  bool valid;          // 1 bit
  uint64_t tag;        // L1I_TIMING_MSHR_TAG_BITS bits
  uint32_t source_set; // 8 bits
  uint32_t source_way; // 6 bits
  uint64_t timestamp;  // L1I_TIME_BITS bits // time when issued
  bool accessed;       // 1 bit
  uint32_t pos_hist;   // 1 bit
} l1i_timing_mshr_entry;

// We do not have access to the cache, so we aproximate it using this structure
typedef struct __l1i_timing_cache_entry {
  bool valid;          // 1 bit
  uint64_t tag;        // L1I_TIMING_CACHE_TAG_BITS bits
  uint32_t source_set; // 8 bits
  uint32_t source_way; // 6 bits
  bool accessed;       // 1 bit
} l1i_timing_cache_entry;

l1i_timing_mshr_entry l1i_timing_mshr_table[NUM_CPUS][L1I_TIMING_MSHR_SIZE];
l1i_timing_cache_entry l1i_timing_cache_table[NUM_CPUS][MAX_NUM_SET][MAX_NUM_WAY];

void l1i_init_timing_tables()
{
  for (uint32_t i = 0; i < L1I_TIMING_MSHR_SIZE; i++) {
    l1i_timing_mshr_table[l1i_cpu_id][i].valid = 0;
  }
  for (uint32_t i = 0; i < MAX_NUM_SET; i++) {
    for (uint32_t j = 0; j < MAX_NUM_WAY; j++) {
      l1i_timing_cache_table[l1i_cpu_id][i][j].valid = 0;
    }
  }
}

uint64_t l1i_find_timing_mshr_entry(uint64_t line_addr)
{
  for (uint32_t i = 0; i < L1I_TIMING_MSHR_SIZE; i++) {
    if (l1i_timing_mshr_table[l1i_cpu_id][i].tag == (line_addr & L1I_TIMING_MSHR_TAG_MASK) && l1i_timing_mshr_table[l1i_cpu_id][i].valid)
      return i;
  }
  return L1I_TIMING_MSHR_SIZE;
}

uint64_t l1i_find_timing_cache_entry(uint64_t line_addr)
{
  uint64_t i = line_addr % MAX_NUM_SET;
  for (uint32_t j = 0; j < MAX_NUM_WAY; j++) {
    if (l1i_timing_cache_table[l1i_cpu_id][i][j].tag == ((line_addr >> L1I_SET_BITS) & L1I_TIMING_CACHE_TAG_MASK)
        && l1i_timing_cache_table[l1i_cpu_id][i][j].valid)
      return j;
  }
  return MAX_NUM_WAY;
}

uint32_t l1i_get_invalid_timing_mshr_entry()
{
  for (uint32_t i = 0; i < L1I_TIMING_MSHR_SIZE; i++) {
    if (!l1i_timing_mshr_table[l1i_cpu_id][i].valid)
      return i;
  }
  assert(false); // It must return a free entry
  return L1I_TIMING_MSHR_SIZE;
}

uint32_t l1i_get_invalid_timing_cache_entry(uint64_t line_addr)
{
  uint32_t i = line_addr % MAX_NUM_SET;
  for (uint32_t j = 0; j < MAX_NUM_WAY; j++) {
    if (!l1i_timing_cache_table[l1i_cpu_id][i][j].valid)
      return j;
  }
  // assert(false); // It must return a free entry
  return MAX_NUM_WAY;
}

void l1i_add_timing_entry(uint64_t line_addr, uint32_t source_set, uint32_t source_way)
{
  // First find for coalescing
  if (l1i_find_timing_mshr_entry(line_addr) < L1I_TIMING_MSHR_SIZE)
    return;
  if (l1i_find_timing_cache_entry(line_addr) < MAX_NUM_WAY)
    return;

  uint32_t i = l1i_get_invalid_timing_mshr_entry();
  l1i_timing_mshr_table[l1i_cpu_id][i].valid = true;
  l1i_timing_mshr_table[l1i_cpu_id][i].tag = line_addr & L1I_TIMING_MSHR_TAG_MASK;
  l1i_timing_mshr_table[l1i_cpu_id][i].source_set = source_set;
  l1i_timing_mshr_table[l1i_cpu_id][i].source_way = source_way;
  l1i_timing_mshr_table[l1i_cpu_id][i].timestamp = l1i_current_cycle & L1I_TIME_MASK;
  l1i_timing_mshr_table[l1i_cpu_id][i].accessed = false;
}

void l1i_invalid_timing_mshr_entry(uint64_t line_addr)
{
  uint32_t index = l1i_find_timing_mshr_entry(line_addr);
  assert(index < L1I_TIMING_MSHR_SIZE);
  l1i_timing_mshr_table[l1i_cpu_id][index].valid = false;
}

void l1i_move_timing_entry(uint64_t line_addr)
{
  uint32_t index_mshr = l1i_find_timing_mshr_entry(line_addr);
  if (index_mshr == L1I_TIMING_MSHR_SIZE) {
    uint32_t set = line_addr % MAX_NUM_SET;
    uint32_t index_cache = l1i_get_invalid_timing_cache_entry(line_addr);
    if (index_cache == MAX_NUM_WAY) {
      return;
    }
    l1i_timing_cache_table[l1i_cpu_id][set][index_cache].valid = true;
    l1i_timing_cache_table[l1i_cpu_id][set][index_cache].tag = (line_addr >> L1I_SET_BITS) & L1I_TIMING_CACHE_TAG_MASK;
    l1i_timing_cache_table[l1i_cpu_id][set][index_cache].source_way = L1I_ENTANGLED_TABLE_WAYS;
    l1i_timing_cache_table[l1i_cpu_id][set][index_cache].accessed = true;
    return;
  }
  uint64_t set = line_addr % MAX_NUM_SET;
  uint64_t index_cache = l1i_get_invalid_timing_cache_entry(line_addr);
  if (index_cache == MAX_NUM_WAY) {
    return;
  }
  l1i_timing_cache_table[l1i_cpu_id][set][index_cache].valid = true;
  l1i_timing_cache_table[l1i_cpu_id][set][index_cache].tag = (line_addr >> L1I_SET_BITS) & L1I_TIMING_CACHE_TAG_MASK;
  l1i_timing_cache_table[l1i_cpu_id][set][index_cache].source_set = l1i_timing_mshr_table[l1i_cpu_id][index_mshr].source_set;
  l1i_timing_cache_table[l1i_cpu_id][set][index_cache].source_way = l1i_timing_mshr_table[l1i_cpu_id][index_mshr].source_way;
  l1i_timing_cache_table[l1i_cpu_id][set][index_cache].accessed = l1i_timing_mshr_table[l1i_cpu_id][index_mshr].accessed;
  l1i_invalid_timing_mshr_entry(line_addr);
}

// returns if accessed
int l1i_invalid_timing_cache_entry(uint64_t line_addr, uint32_t& source_set, uint32_t& source_way)
{
  uint32_t set = line_addr % MAX_NUM_SET;
  uint32_t way = l1i_find_timing_cache_entry(line_addr);
  // assert(way < MAX_NUM_WAY);
  if (way >= MAX_NUM_WAY) {
    return -1;
  }
  l1i_timing_cache_table[l1i_cpu_id][set][way].valid = false;
  source_set = l1i_timing_cache_table[l1i_cpu_id][set][way].source_set;
  source_way = l1i_timing_cache_table[l1i_cpu_id][set][way].source_way;
  return l1i_timing_cache_table[l1i_cpu_id][set][way].accessed;
}

void l1i_access_timing_entry(uint64_t line_addr, uint32_t pos_hist)
{
  uint32_t index = l1i_find_timing_mshr_entry(line_addr);
  if (index < L1I_TIMING_MSHR_SIZE) {
    if (!l1i_timing_mshr_table[l1i_cpu_id][index].accessed) {
      l1i_timing_mshr_table[l1i_cpu_id][index].accessed = true;
      l1i_timing_mshr_table[l1i_cpu_id][index].pos_hist = pos_hist;
    }
    return;
  }
  uint32_t set = line_addr % MAX_NUM_SET;
  uint32_t way = l1i_find_timing_cache_entry(line_addr);
  if (way < MAX_NUM_WAY) {
    l1i_timing_cache_table[l1i_cpu_id][set][way].accessed = true;
  }
}

bool l1i_is_accessed_timing_entry(uint64_t line_addr)
{
  uint32_t index = l1i_find_timing_mshr_entry(line_addr);
  if (index < L1I_TIMING_MSHR_SIZE) {
    return l1i_timing_mshr_table[l1i_cpu_id][index].accessed;
  }
  uint32_t set = line_addr % MAX_NUM_SET;
  uint32_t way = l1i_find_timing_cache_entry(line_addr);
  if (way < MAX_NUM_WAY) {
    return l1i_timing_cache_table[l1i_cpu_id][set][way].accessed;
  }
  return false;
}

bool l1i_completed_request(uint64_t line_addr) { return l1i_find_timing_cache_entry(line_addr) < MAX_NUM_WAY; }

bool l1i_ongoing_request(uint64_t line_addr) { return l1i_find_timing_mshr_entry(line_addr) < L1I_TIMING_MSHR_SIZE; }

bool l1i_ongoing_accessed_request(uint64_t line_addr)
{
  uint32_t index = l1i_find_timing_mshr_entry(line_addr);
  if (index == L1I_TIMING_MSHR_SIZE)
    return false;
  return l1i_timing_mshr_table[l1i_cpu_id][index].accessed;
}

uint64_t l1i_get_latency_timing_mshr(uint64_t line_addr, uint32_t& pos_hist)
{
  uint32_t index = l1i_find_timing_mshr_entry(line_addr);
  if (index == L1I_TIMING_MSHR_SIZE)
    return 0;
  if (!l1i_timing_mshr_table[l1i_cpu_id][index].accessed)
    return 0;
  pos_hist = l1i_timing_mshr_table[l1i_cpu_id][index].pos_hist;
  return l1i_get_latency(l1i_current_cycle, l1i_timing_mshr_table[l1i_cpu_id][index].timestamp);
}

// RECORD ENTANGLED TABLE

uint32_t L1I_ENTANGLED_FORMATS[L1I_ENTANGLED_MAX_FORMATS] = {58, 28, 18, 13, 10, 8, 6};
#define L1I_ENTANGLED_NUM_FORMATS 6

// uint32_t L1I_ENTANGLED_FORMATS[L1I_ENTANGLED_MAX_FORMATS] = {43, 20, 13, 9, 6, 5};
// #define L1I_ENTANGLED_NUM_FORMATS 6

// uint32_t L1I_ENTANGLED_FORMATS[L1I_ENTANGLED_MAX_FORMATS] = {42, 20, 12, 9};
// #define L1I_ENTANGLED_NUM_FORMATS 4

// uint32_t L1I_ENTANGLED_FORMATS[L1I_ENTANGLED_MAX_FORMATS] = {43, 0, 13, 9, 7};
// #define L1I_ENTANGLED_NUM_FORMATS 5

uint32_t l1i_get_format_entangled(uint64_t line_addr, uint64_t entangled_addr)
{
  for (uint32_t i = L1I_ENTANGLED_NUM_FORMATS; i != 0; i--) {
    if ((line_addr >> L1I_ENTANGLED_FORMATS[i - 1]) == (entangled_addr >> L1I_ENTANGLED_FORMATS[i - 1])) {
      return i;
    }
  }
  assert(false);
}

uint64_t l1i_extend_format_entangled(uint64_t line_addr, uint64_t entangled_addr, uint32_t format)
{
  return ((line_addr >> L1I_ENTANGLED_FORMATS[format - 1]) << L1I_ENTANGLED_FORMATS[format - 1])
         | (entangled_addr & (((uint64_t)1 << L1I_ENTANGLED_FORMATS[format - 1]) - 1));
}

uint64_t l1i_compress_format_entangled(uint64_t entangled_addr, uint32_t format)
{
  return entangled_addr & (((uint64_t)1 << L1I_ENTANGLED_FORMATS[format - 1]) - 1);
}

#define L1I_ENTANGLED_TABLE_INDEX_BITS 8
#define L1I_ENTANGLED_TABLE_SETS (1 << L1I_ENTANGLED_TABLE_INDEX_BITS)
#define L1I_MAX_ENTANGLED_PER_LINE L1I_ENTANGLED_NUM_FORMATS
#define L1I_TAG_BITS (18 - L1I_ENTANGLED_TABLE_INDEX_BITS)
#define L1I_TAG_MASK (((uint64_t)1 << L1I_TAG_BITS) - 1)
#define L1I_CONFIDENCE_COUNTER_BITS 2
#define L1I_CONFIDENCE_COUNTER_MAX_VALUE ((1 << L1I_CONFIDENCE_COUNTER_BITS) - 1)
#define L1I_CONFIDENCE_COUNTER_THRESHOLD 1

#define L1I_TRIES_AVAIL_ENTANGLED 2
#define L1I_TRIES_AVAIL_ENTANGLED_NOT_PRESENT 1

typedef struct __l1i_entangled_entry {
  uint64_t tag;                                        // L1I_TAG_BITS bits
  uint32_t format;                                     // 3 bits
  uint64_t entangled_addr[L1I_MAX_ENTANGLED_PER_LINE]; // keep just diff
  uint32_t entangled_conf[L1I_MAX_ENTANGLED_PER_LINE]; // L1I_CONFIDENCE_COUNTER_BITS bits
  uint32_t bb_size;                                    // L1I_MERGE_BBSIZE_BITS bits
} l1i_entangled_entry;

l1i_entangled_entry l1i_entangled_table[NUM_CPUS][L1I_ENTANGLED_TABLE_SETS][L1I_ENTANGLED_TABLE_WAYS];
uint32_t l1i_entangled_fifo[NUM_CPUS][L1I_ENTANGLED_TABLE_SETS]; // log2(L1I_ENTANGLED_TABLE_WAYS) * L1I_ENTANGLED_TABLE_SETS bits

uint64_t l1i_hash(uint64_t line_addr)
{
  return line_addr ^ (line_addr >> 2) ^ (line_addr >> 5);
  // return line_addr ^ (line_addr >> 18);
}

void l1i_init_entangled_table()
{
  for (uint32_t i = 0; i < L1I_ENTANGLED_TABLE_SETS; i++) {
    for (uint32_t j = 0; j < L1I_ENTANGLED_TABLE_WAYS; j++) {
      l1i_entangled_table[l1i_cpu_id][i][j].tag = 0;
      l1i_entangled_table[l1i_cpu_id][i][j].format = 1;
      for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
        l1i_entangled_table[l1i_cpu_id][i][j].entangled_addr[k] = 0;
        l1i_entangled_table[l1i_cpu_id][i][j].entangled_conf[k] = 0;
      }
      l1i_entangled_table[l1i_cpu_id][i][j].bb_size = 0;
    }
    l1i_entangled_fifo[l1i_cpu_id][i] = 0;
  }
}

uint32_t l1i_get_way_entangled_table(uint64_t line_addr)
{
  uint64_t tag = (l1i_hash(line_addr) >> L1I_ENTANGLED_TABLE_INDEX_BITS) & L1I_TAG_MASK;
  uint32_t set = l1i_hash(line_addr) % L1I_ENTANGLED_TABLE_SETS;
  for (uint32_t i = 0; i < L1I_ENTANGLED_TABLE_WAYS; i++) {
    if (l1i_entangled_table[l1i_cpu_id][set][i].tag == tag) { // Found
      return i;
    }
  }
  return L1I_ENTANGLED_TABLE_WAYS;
}

void l1i_try_realocate_evicted_in_available_entangled_table(uint32_t set)
{
  uint64_t way = l1i_entangled_fifo[l1i_cpu_id][set];
  bool dest_free_way = true;
  for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
    if (l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
      dest_free_way = false;
      break;
    }
  }
  if (dest_free_way && l1i_entangled_table[l1i_cpu_id][set][way].bb_size == 0)
    return;
  uint32_t free_way = way;
  bool free_with_size = false;
  for (uint32_t i = (way + 1) % L1I_ENTANGLED_TABLE_WAYS; i != way; i = (i + 1) % L1I_ENTANGLED_TABLE_WAYS) {
    bool dest_free = true;
    for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
      if (l1i_entangled_table[l1i_cpu_id][set][i].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
        dest_free = false;
        break;
      }
    }
    if (dest_free) {
      if (free_way == way) {
        free_way = i;
        free_with_size = (l1i_entangled_table[l1i_cpu_id][set][i].bb_size != 0);
      } else if (free_with_size && l1i_entangled_table[l1i_cpu_id][set][i].bb_size == 0) {
        free_way = i;
        free_with_size = false;
        break;
      }
    }
  }
  if (free_way != way && ((!free_with_size) || (free_with_size && !dest_free_way))) { // Only evict if it has more information
    l1i_entangled_table[l1i_cpu_id][set][free_way].tag = l1i_entangled_table[l1i_cpu_id][set][way].tag;
    l1i_entangled_table[l1i_cpu_id][set][free_way].format = l1i_entangled_table[l1i_cpu_id][set][way].format;
    for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
      l1i_entangled_table[l1i_cpu_id][set][free_way].entangled_addr[k] = l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k];
      l1i_entangled_table[l1i_cpu_id][set][free_way].entangled_conf[k] = l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k];
    }
    l1i_entangled_table[l1i_cpu_id][set][free_way].bb_size = l1i_entangled_table[l1i_cpu_id][set][way].bb_size;
  }
}

void l1i_add_entangled_table(uint64_t line_addr, uint64_t entangled_addr)
{
  uint64_t tag = (l1i_hash(line_addr) >> L1I_ENTANGLED_TABLE_INDEX_BITS) & L1I_TAG_MASK;
  uint32_t set = l1i_hash(line_addr) % L1I_ENTANGLED_TABLE_SETS;
  uint32_t way = l1i_get_way_entangled_table(line_addr);
  if (way == L1I_ENTANGLED_TABLE_WAYS) {
    l1i_try_realocate_evicted_in_available_entangled_table(set);
    way = l1i_entangled_fifo[l1i_cpu_id][set];
    l1i_entangled_table[l1i_cpu_id][set][way].tag = tag;
    l1i_entangled_table[l1i_cpu_id][set][way].format = 1;
    for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
      l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k] = 0;
      l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] = 0;
    }
    l1i_entangled_table[l1i_cpu_id][set][way].bb_size = 0;
    l1i_entangled_fifo[l1i_cpu_id][set] = (l1i_entangled_fifo[l1i_cpu_id][set] + 1) % L1I_ENTANGLED_TABLE_WAYS;
  }
  for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
    if (l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD
        && l1i_extend_format_entangled(line_addr, l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k], l1i_entangled_table[l1i_cpu_id][set][way].format)
               == entangled_addr) {
      l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] = L1I_CONFIDENCE_COUNTER_MAX_VALUE;
      return;
    }
  }

  // Adding a new entangled
  uint32_t format_new = l1i_get_format_entangled(line_addr, entangled_addr);
  l1i_stats_formats[format_new - 1]++;

  // Check for evictions
  while (true) {
    uint32_t min_format = format_new;
    uint32_t num_valid = 1;
    uint32_t min_value = L1I_CONFIDENCE_COUNTER_MAX_VALUE + 1;
    uint32_t min_pos = 0;
    for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
      if (l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
        num_valid++;
        uint32_t format_k =
            l1i_get_format_entangled(line_addr, l1i_extend_format_entangled(line_addr, l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k],
                                                                            l1i_entangled_table[l1i_cpu_id][set][way].format));
        if (format_k < min_format) {
          min_format = format_k;
        }
        if (l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] < min_value) {
          min_value = l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k];
          min_pos = k;
        }
      }
    }
    if (num_valid > min_format) { // Eviction is necessary. We chose the lower confidence one
      l1i_stats_evict_entangled_k_table++;
      l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[min_pos] = 0;
    } else {
      // Reformat
      for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
        if (l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
          l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k] =
              l1i_compress_format_entangled(l1i_extend_format_entangled(line_addr, l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k],
                                                                        l1i_entangled_table[l1i_cpu_id][set][way].format),
                                            min_format);
        }
      }
      l1i_entangled_table[l1i_cpu_id][set][way].format = min_format;
      break;
    }
  }
  for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
    if (l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] < L1I_CONFIDENCE_COUNTER_THRESHOLD) {
      l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k] =
          l1i_compress_format_entangled(entangled_addr, l1i_entangled_table[l1i_cpu_id][set][way].format);
      l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] = L1I_CONFIDENCE_COUNTER_MAX_VALUE;
      return;
    }
  }
}

bool l1i_avail_entangled_table(uint64_t line_addr, uint64_t entangled_addr, bool insert_not_present)
{
  uint32_t set = l1i_hash(line_addr) % L1I_ENTANGLED_TABLE_SETS;
  uint32_t way = l1i_get_way_entangled_table(line_addr);
  if (way == L1I_ENTANGLED_TABLE_WAYS)
    return insert_not_present;
  for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
    if (l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD
        && l1i_extend_format_entangled(line_addr, l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k], l1i_entangled_table[l1i_cpu_id][set][way].format)
               == entangled_addr) {
      return true;
    }
  }
  // Check for availability
  uint32_t min_format = l1i_get_format_entangled(line_addr, entangled_addr);
  uint32_t num_valid = 1;
  for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
    if (l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
      num_valid++;
      uint32_t format_k =
          l1i_get_format_entangled(line_addr, l1i_extend_format_entangled(line_addr, l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k],
                                                                          l1i_entangled_table[l1i_cpu_id][set][way].format));
      if (format_k < min_format) {
        min_format = format_k;
      }
    }
  }
  if (num_valid > min_format) { // Eviction is necessary
    return false;
  } else {
    return true;
  }
}

void l1i_add_bbsize_table(uint64_t line_addr, uint32_t bb_size)
{
  uint64_t tag = (l1i_hash(line_addr) >> L1I_ENTANGLED_TABLE_INDEX_BITS) & L1I_TAG_MASK;
  uint32_t set = l1i_hash(line_addr) % L1I_ENTANGLED_TABLE_SETS;
  uint32_t way = l1i_get_way_entangled_table(line_addr);
  if (way == L1I_ENTANGLED_TABLE_WAYS) {
    l1i_try_realocate_evicted_in_available_entangled_table(set);
    way = l1i_entangled_fifo[l1i_cpu_id][set];
    l1i_entangled_table[l1i_cpu_id][set][way].tag = tag;
    l1i_entangled_table[l1i_cpu_id][set][way].format = 1;
    for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
      l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k] = 0;
      l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] = 0;
    }
    l1i_entangled_table[l1i_cpu_id][set][way].bb_size = 0;
    l1i_entangled_fifo[l1i_cpu_id][set] = (l1i_entangled_fifo[l1i_cpu_id][set] + 1) % L1I_ENTANGLED_TABLE_WAYS;
  }
  if (bb_size > l1i_entangled_table[l1i_cpu_id][set][way].bb_size) {
    l1i_entangled_table[l1i_cpu_id][set][way].bb_size = bb_size & L1I_MERGE_BBSIZE_MAX_VALUE;
  }
  if (bb_size > l1i_stats_max_bb_size) {
    l1i_stats_max_bb_size = bb_size;
  }
}

uint64_t l1i_get_entangled_addr_entangled_table(uint64_t line_addr, uint32_t index_k, uint32_t& set, uint32_t& way)
{
  set = l1i_hash(line_addr) % L1I_ENTANGLED_TABLE_SETS;
  way = l1i_get_way_entangled_table(line_addr);
  if (way < L1I_ENTANGLED_TABLE_WAYS) {
    if (l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[index_k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
      return l1i_extend_format_entangled(line_addr, l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[index_k],
                                         l1i_entangled_table[l1i_cpu_id][set][way].format);
    }
  }
  return 0;
}

uint32_t l1i_get_bbsize_entangled_table(uint64_t line_addr)
{
  uint32_t set = l1i_hash(line_addr) % L1I_ENTANGLED_TABLE_SETS;
  uint32_t way = l1i_get_way_entangled_table(line_addr);
  if (way < L1I_ENTANGLED_TABLE_WAYS) {
    return l1i_entangled_table[l1i_cpu_id][set][way].bb_size;
  }
  return 0;
}

void l1i_update_confidence_entangled_table(uint32_t set, uint32_t way, uint64_t entangled_addr, bool accessed)
{
  if (way < L1I_ENTANGLED_TABLE_WAYS) {
    for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
      if (l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD
          //&& (l1i_extend_format_entangled(((l1i_entangled_table[l1i_cpu_id][set][way].tag << L1I_ENTANGLED_TABLE_INDEX_BITS) | set),
          //l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k], l1i_entangled_table[l1i_cpu_id][set][way].format) & L1I_TIMING_MSHR_TAG_MASK) ==
          //(entangled_addr & L1I_TIMING_MSHR_TAG_MASK)) {
          //&& (l1i_extend_format_entangled(((l1i_entangled_table[l1i_cpu_id][set][way].tag << L1I_ENTANGLED_TABLE_INDEX_BITS) | set),
          //l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k], l1i_entangled_table[l1i_cpu_id][set][way].format) & L1I_TAG_MASK) == (entangled_addr &
          //L1I_TAG_MASK)) {
          && l1i_compress_format_entangled(l1i_entangled_table[l1i_cpu_id][set][way].entangled_addr[k], l1i_entangled_table[l1i_cpu_id][set][way].format)
                 == l1i_compress_format_entangled(entangled_addr, l1i_entangled_table[l1i_cpu_id][set][way].format)) {
        if (accessed && l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] < L1I_CONFIDENCE_COUNTER_MAX_VALUE) {
          l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k]++;
        }
        if (!accessed && l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k] > 0) {
          l1i_entangled_table[l1i_cpu_id][set][way].entangled_conf[k]--;
        }
      }
    }
  }
}

// INTERFACE

void CACHE::prefetcher_initialize()
{
  std::cout << "CPU " << cpu << " EPI prefetcher" << std::endl;

  assert(MAX_PQ_SIZE == PQ_SIZE);
  // assert(MAX_RQ_SIZE == RQ_SIZE);
  assert(MAX_NUM_SET == NUM_SET);
  assert(MAX_NUM_WAY == NUM_WAY);

  l1i_cpu_id = cpu;
  l1i_current_cycle = current_cycle;

  l1i_init_stats_table();
  l1i_last_basic_block = 0;
  l1i_consecutive_count = 0;
  l1i_basic_block_merge_diff = 0;

  l1i_init_hist_table();
  l1i_init_timing_tables();
  l1i_init_entangled_table();
}

void CACHE::prefetcher_branch_operate(uint64_t ip, uint8_t branch_type, uint64_t branch_target) {}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, bool prefetch_hit, uint8_t type, uint32_t metadata_in)
{
  l1i_cpu_id = cpu;
  l1i_current_cycle = current_cycle;

  uint64_t line_addr = addr >> LOG2_BLOCK_SIZE;

  // if (!cache_hit) assert(!prefetch_hit);
  if (!cache_hit) {
    // assert(l1i_find_timing_cache_entry(line_addr) == MAX_NUM_WAY);
    if (l1i_find_timing_cache_entry(line_addr) > MAX_NUM_WAY || l1i_find_timing_mshr_entry(line_addr) < L1I_TIMING_MSHR_SIZE) {
      return metadata_in;
    }
  }
  if (cache_hit) {
    // assert(l1i_find_timing_cache_entry(line_addr) < MAX_NUM_WAY);
    if (l1i_find_timing_cache_entry(line_addr) >= MAX_NUM_WAY) {
      return metadata_in;
    }
  }

  l1i_stats_table[cpu][(line_addr & L1I_STATS_TABLE_MASK)].accesses++;
  if (!cache_hit) {
    l1i_stats_table[cpu][(line_addr & L1I_STATS_TABLE_MASK)].misses++;
    if (l1i_ongoing_request(line_addr) && !l1i_is_accessed_timing_entry(line_addr)) {
      l1i_stats_table[cpu][(line_addr & L1I_STATS_TABLE_MASK)].late++;
    }
  }
  if (cache_hit && prefetch_hit) {
    l1i_stats_table[cpu][(line_addr & L1I_STATS_TABLE_MASK)].hits++;
  }

  bool consecutive = false;

  if (l1i_last_basic_block + l1i_consecutive_count == line_addr) { // Same
    return metadata_in;
  } else if (l1i_last_basic_block + l1i_consecutive_count + 1 == line_addr) { // Consecutive
    l1i_consecutive_count++;
    consecutive = true;
  }

  // Queue basic block prefetches
  uint32_t bb_size = l1i_get_bbsize_entangled_table(line_addr);
  if (bb_size)
    l1i_stats_basic_blocks[bb_size]++;
  for (uint32_t i = 1; i <= bb_size; i++) {
    uint64_t pf_addr = addr + i * (1 << LOG2_BLOCK_SIZE);
    if (!l1i_ongoing_request(pf_addr >> LOG2_BLOCK_SIZE)) {
      if (prefetch_line(pf_addr, true, 0)) {
        // cout<< "prefchB: " << std::hex << (pf_addr >> LOG2_BLOCK_SIZE) << std::dec << std::endl;
        l1i_add_timing_entry(pf_addr >> LOG2_BLOCK_SIZE, 0, L1I_ENTANGLED_TABLE_WAYS);
      } else {
        // cout<< "prefchQ: " << std::hex << (pf_addr >> LOG2_BLOCK_SIZE) << std::dec << std::endl;
      }
    }
  }

  // Queue entangled and basic block of entangled prefetches
  uint32_t num_entangled = 0;
  for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
    uint32_t source_set = 0;
    uint32_t source_way = L1I_ENTANGLED_TABLE_WAYS;
    uint64_t entangled_line_addr = l1i_get_entangled_addr_entangled_table(line_addr, k, source_set, source_way);
    if (entangled_line_addr && (entangled_line_addr != line_addr)) {
      num_entangled++;
      uint32_t bb_size = l1i_get_bbsize_entangled_table(entangled_line_addr);
      if (bb_size)
        l1i_stats_basic_blocks_ent[bb_size]++;
      for (uint32_t i = 0; i <= bb_size; i++) {
        uint64_t pf_line_addr = entangled_line_addr + i;
        if (!l1i_ongoing_request(pf_line_addr)) {
          if (prefetch_line(pf_line_addr << LOG2_BLOCK_SIZE, true, 0)) {
            // cout<< "prefchE: " << std::hex << pf_line_addr << std::dec << std::endl;
            l1i_add_timing_entry(pf_line_addr, source_set, (i == 0) ? source_way : L1I_ENTANGLED_TABLE_WAYS);
          } else {
            // cout<< "prefchQ: " << std::hex << pf_line_addr << std::dec << std::endl;
          }
        }
      }
    }
  }
  if (num_entangled)
    l1i_stats_entangled[num_entangled]++;

  if (!consecutive) { // New basic block found
    uint32_t max_bb_size = l1i_get_bbsize_entangled_table(l1i_last_basic_block);

    // Check for merging bb opportunities
    if (l1i_consecutive_count) { // single blocks no need to merge and are not inserted in the entangled table
      if (l1i_basic_block_merge_diff > 0) {
        l1i_add_bbsize_table(l1i_last_basic_block - l1i_basic_block_merge_diff, l1i_consecutive_count + l1i_basic_block_merge_diff);
        l1i_add_bb_size_hist_table(l1i_last_basic_block - l1i_basic_block_merge_diff, l1i_consecutive_count + l1i_basic_block_merge_diff);
      } else {
        l1i_add_bbsize_table(l1i_last_basic_block, std::max(max_bb_size, l1i_consecutive_count));
        l1i_add_bb_size_hist_table(l1i_last_basic_block, std::max(max_bb_size, l1i_consecutive_count));
      }
    }
  }

  if (!consecutive) { // New basic block found
    l1i_consecutive_count = 0;
    l1i_last_basic_block = line_addr;
  }

  if (!consecutive) {
    l1i_basic_block_merge_diff = l1i_find_bb_merge_hist_table(l1i_last_basic_block);
  }

  // Add the request in the history buffer
  uint32_t pos_hist = L1I_HIST_TABLE_ENTRIES;
  if (!consecutive && l1i_basic_block_merge_diff == 0) {
    if ((l1i_find_hist_entry(line_addr) == L1I_HIST_TABLE_ENTRIES)) {
      pos_hist = l1i_add_hist_table(line_addr);
    } else {
      if (!cache_hit && !l1i_ongoing_accessed_request(line_addr)) {
        pos_hist = l1i_add_hist_table(line_addr);
      }
    }
  }

  // Add miss in the latency table
  if (!cache_hit && !l1i_ongoing_request(line_addr)) {
    l1i_add_timing_entry(line_addr, 0, L1I_ENTANGLED_TABLE_WAYS);
  }
  l1i_access_timing_entry(line_addr, pos_hist);

  return metadata_in;
}

void CACHE::prefetcher_cycle_operate()
{
  l1i_stats_cycle_operate++;
  l1i_stats_cycle_no_operate += (current_cycle - (l1i_stats_last_cycle_operate[cpu] + 1));
  l1i_stats_last_cycle_operate[cpu] = current_cycle;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{
  l1i_cpu_id = cpu;
  l1i_current_cycle = current_cycle;

  uint64_t line_addr = (addr >> LOG2_BLOCK_SIZE);
  uint64_t evicted_line_addr = (evicted_addr >> LOG2_BLOCK_SIZE);

  // Line is in cache
  if (evicted_addr) {
    uint32_t source_set = 0;
    uint32_t source_way = L1I_ENTANGLED_TABLE_WAYS;
    int accessed = l1i_invalid_timing_cache_entry(evicted_line_addr, source_set, source_way);
    if (accessed == -1) {
      return metadata_in;
    }

    if (!accessed) {
      l1i_stats_table[cpu][(evicted_line_addr & L1I_STATS_TABLE_MASK)].wrong++;
    }
    if (source_way < L1I_ENTANGLED_TABLE_WAYS) {
      // If accessed hit, but if not wrong
      l1i_update_confidence_entangled_table(source_set, source_way, evicted_line_addr, accessed);
    }
  }

  uint32_t pos_hist = L1I_HIST_TABLE_ENTRIES;
  uint64_t latency = l1i_get_latency_timing_mshr(line_addr, pos_hist);

  l1i_move_timing_entry(line_addr);

  // Get and update entangled
  if (latency && pos_hist < L1I_HIST_TABLE_ENTRIES) {
    bool inserted = false;
    for (uint32_t i = 0; i < L1I_TRIES_AVAIL_ENTANGLED; i++) {
      uint64_t bere = l1i_get_bere_hist_table(line_addr, pos_hist, latency, i);
      if (bere == -1) {
        continue;
      }
      if (bere && line_addr != bere) {
        if (l1i_avail_entangled_table(bere, line_addr, false)) {
          l1i_add_entangled_table(bere, line_addr);
          inserted = true;
          break;
        }
      }
    }
    if (!inserted) {
      for (uint32_t i = 0; i < L1I_TRIES_AVAIL_ENTANGLED_NOT_PRESENT; i++) {
        uint64_t bere = l1i_get_bere_hist_table(line_addr, pos_hist, latency, i);
        if (bere == -1) {
          continue;
        }
        if (bere && line_addr != bere) {
          if (l1i_avail_entangled_table(bere, line_addr, true)) {
            l1i_add_entangled_table(bere, line_addr);
            inserted = true;
            break;
          }
        }
      }
    }
    if (!inserted) {
      uint64_t bere = l1i_get_bere_hist_table(line_addr, pos_hist, latency);
      if (bere == -1) {
        return metadata_in;
      }
      if (bere && line_addr != bere) {
        l1i_add_entangled_table(bere, line_addr);
      }
    }
  }

  return metadata_in;
}

void CACHE::prefetcher_final_stats()
{
  std::cout << "CPU " << cpu << " L1I EPI prefetcher final stats" << std::endl;
  l1i_print_stats_table();
}

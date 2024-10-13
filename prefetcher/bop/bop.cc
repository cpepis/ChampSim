#include "bop.h"

#include <cassert>
#include <cstring>
#include <iostream>

#include "cache.h"
#include "champsim.h"

namespace knob
{
vector<int32_t> bop_candidates = {1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 16, 18, 20, 24, 25, 27, 30, 32, 36, 40, 45, 48, 50, 54, 60};
uint32_t bop_max_rounds = 100;
uint32_t bop_max_score = 31;
uint32_t bop_top_n = 1;
bool bop_enable_pref_buffer = false;
uint32_t bop_pref_buffer_size = 256;
uint32_t bop_pref_degree = 4;
uint32_t bop_rr_size = 256;
} // namespace knob

void BOPrefetcher::init_knobs() {}

void BOPrefetcher::init_stats() { memset(&stats, 0, sizeof(stats)); }

BOPrefetcher::BOPrefetcher()
{
  init_knobs();
  init_stats();
  print_config();

  round_counter = 0;
  candidate_ptr = 0;
  scores.resize(knob::bop_candidates.size(), 0);
  best_offsets.push_back(1); // for initial prefetches
}

BOPrefetcher::~BOPrefetcher() {}

void BOPrefetcher::print_config()
{
  cout << "bop_max_rounds " << knob::bop_max_rounds << endl
       << "bop_max_score " << knob::bop_max_score << endl
       << "bop_top_n " << knob::bop_top_n << endl
       << "bop_enable_pref_buffer " << knob::bop_enable_pref_buffer << endl
       << "bop_pref_buffer_size " << knob::bop_pref_buffer_size << endl
       << "bop_pref_degree " << knob::bop_pref_degree << endl
       << "bop_rr_size " << knob::bop_rr_size << endl
       << "bop_candidates ";
  for (uint32_t index = 0; index < knob::bop_candidates.size(); ++index) {
    cout << knob::bop_candidates[index] << ",";
  }
  cout << endl << endl;
}

void BOPrefetcher::invoke_prefetcher(uint64_t pc, uint64_t address, uint8_t cache_hit, uint8_t type, vector<uint64_t>& pref_addr)
{
  uint64_t page = address >> LOG2_PAGE_SIZE;
  uint32_t offset = (address >> LOG2_BLOCK_SIZE) & ((1ull << (LOG2_PAGE_SIZE - LOG2_BLOCK_SIZE)) - 1);
  uint64_t ca_address = (address >> LOG2_BLOCK_SIZE);

  /* Evaluate a candidate offset */
  int32_t offset_to_evaluate = knob::bop_candidates[candidate_ptr];
  if (search_rr(ca_address - offset_to_evaluate)) {
    scores[candidate_ptr]++;
  }
  candidate_ptr++;
  candidate_ptr = candidate_ptr % knob::bop_candidates.size();
  round_counter++;

  /* check for end of phase */
  if (check_end_of_phase()) {
    phase_end();
  }

  /* issue prefetches */
  for (uint32_t index = 0; index < best_offsets.size(); ++index) {
    int32_t pf_offset = offset + best_offsets[index];
    if (pf_offset >= 0 && pf_offset < 64) {
      uint64_t addr = (page << LOG2_PAGE_SIZE) + (pf_offset << LOG2_BLOCK_SIZE);
      pref_addr.push_back(addr);
    }
  }

  if (knob::bop_enable_pref_buffer) {
    buffer_prefetch(pref_addr);
    pref_addr.clear();
    issue_prefetch(pref_addr);
  }

  stats.pref_issued += pref_addr.size();
}

bool BOPrefetcher::check_end_of_phase()
{
  if (round_counter >= knob::bop_max_rounds) {
    stats.end_phase.max_round++;
    return true;
  }
  uint32_t max_score = 0;
  for (uint32_t index = 0; index < scores.size(); ++index) {
    if (scores[index] >= max_score) {
      max_score = index;
    }
  }
  if (max_score >= knob::bop_max_score) {
    stats.end_phase.max_score++;
    return true;
  }
  return false;
}

void BOPrefetcher::phase_end()
{
  stats.total_phases++;
  best_offsets.clear();

  uint32_t max = UINT32_MAX, curr_max = 0, max_idx = 0;
  for (uint32_t index = 0; index < knob::bop_top_n; ++index) {
    curr_max = 0;
    max_idx = 0;
    for (uint32_t index2 = 0; index2 < scores.size(); ++index2) {
      if (scores[index2] >= curr_max && scores[index2] < max) {
        curr_max = scores[index2];
        max_idx = index2;
      }
    }
    best_offsets.push_back(knob::bop_candidates[max_idx]);
    max = curr_max;
  }
  assert(best_offsets.size() == knob::bop_top_n);

  scores.clear();
  scores.resize(knob::bop_candidates.size(), 0);
  round_counter = 0;
  candidate_ptr = 0;
}

bool BOPrefetcher::search_rr(uint64_t address)
{
  for (uint32_t index = 0; index < rr.size(); ++index) {
    if (rr[index] == address) {
      return true;
    }
  }
  return false;
}

void BOPrefetcher::buffer_prefetch(vector<uint64_t> pref_addr)
{
  uint32_t count = 0;
  for (uint32_t index = 0; index < pref_addr.size(); ++index) {
    if (pref_buffer.size() >= knob::bop_pref_buffer_size) {
      break;
    }
    pref_buffer.push_back(pref_addr[index]);
    count++;
  }
  stats.pref_buffer.buffered += count;
  stats.pref_buffer.spilled += (pref_addr.size() - count);
}

void BOPrefetcher::issue_prefetch(vector<uint64_t>& pref_addr)
{
  uint32_t count = 0;
  while (!pref_buffer.empty() && count < knob::bop_pref_degree) {
    pref_addr.push_back(pref_buffer.front());
    pref_buffer.pop_front();
    count++;
  }
  stats.pref_buffer.issued += pref_addr.size();
}

void BOPrefetcher::register_fill(uint64_t address)
{
  stats.fill.called++;
  address = (address >> LOG2_BLOCK_SIZE) << LOG2_BLOCK_SIZE;
  uint32_t offset = (address >> LOG2_BLOCK_SIZE) & ((1ull << (LOG2_PAGE_SIZE - LOG2_BLOCK_SIZE)) - 1);
  uint64_t ca_address = (address >> LOG2_BLOCK_SIZE);

  for (uint32_t index = 0; index < best_offsets.size(); ++index) {
    int32_t origin = offset - best_offsets[index];
    if (origin >= 0 && origin < 64) {
      stats.fill.insert_rr++;
      insert_rr(ca_address - best_offsets[index]);
    }
  }
}

void BOPrefetcher::insert_rr(uint64_t address)
{
  if (search_rr(address)) {
    stats.insert_rr.hit++;
    return;
  }
  if (rr.size() >= knob::bop_rr_size) {
    stats.insert_rr.evict++;
    rr.pop_front();
  }
  stats.insert_rr.insert++;
  rr.push_back(address);
}

void BOPrefetcher::dump_stats()
{
  cout << "bop_end_phase_max_round " << stats.end_phase.max_round << endl
       << "bop_end_phase_max_score " << stats.end_phase.max_score << endl
       << "bop_total_phases " << stats.total_phases << endl
       << "bop_fill_called " << stats.fill.called << endl
       << "bop_fill_insert_rr " << stats.fill.insert_rr << endl
       << "bop_insert_rr_hit " << stats.insert_rr.hit << endl
       << "bop_insert_rr_evict " << stats.insert_rr.evict << endl
       << "bop_insert_rr_insert " << stats.insert_rr.insert << endl
       << "bop_pref_buffer_buffered " << stats.pref_buffer.buffered << endl
       << "bop_pref_buffer_spilled " << stats.pref_buffer.spilled << endl
       << "bop_pref_buffer_issued " << stats.pref_buffer.issued << endl
       << "bop_pref_issued " << stats.pref_issued << endl
       << endl;
}

BOPrefetcher* prefetcher;

void CACHE::prefetcher_initialize() { prefetcher = new BOPrefetcher(); }

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, bool useful_prefetch, uint8_t type, uint32_t metadata_in)
{
  vector<uint64_t> pref_addr;
  prefetcher->invoke_prefetcher(ip, addr, cache_hit, type, pref_addr);
  if (!pref_addr.empty()) {
    for (uint32_t addr_index = 0; addr_index < pref_addr.size(); addr_index++) {
      prefetch_line(pref_addr[addr_index], true, 0);
    }
  }
  return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{
  prefetcher->register_fill(addr);
  return metadata_in;
}

void CACHE::prefetcher_cycle_operate() {}

void CACHE::prefetcher_final_stats() { prefetcher->dump_stats(); }

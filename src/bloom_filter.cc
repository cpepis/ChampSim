#include "bloom_filter.h"

#include <algorithm>

#include "champsim.h"
#include <fmt/core.h>

BloomFilter::BloomFilter(LoadPredictorStats& s_lp, size_t _size, size_t _maxEntries)
    : LoadPredictor(s_lp), filter(_size, false), filterSize(_size), maxEntries(_maxEntries), entriesSeen(0)
{
  setEnabled(true);
  fmt::print("Bloom filter size: {} max entries: {}\n", filterSize, maxEntries);
}

void BloomFilter::update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit)
{
  // Check if the predictor is enabled using the base class method
  if (!isEnabled()) {
    return; // No update if the predictor is disabled
  }

  lp_stats.record_outcome(actual_was_hit, predicted_was_hit);

  // Insert the address into the Bloom filter if it was a hit
  if (actual_was_hit) 
    insert(addr);

  if constexpr (champsim::debug_print) {
    fmt::print("[BLOOM] {}: Updating for address: 0x{:x} (PC: 0x{:x}) - Actual Hit: {}, Predicted Hit: {}\n", __func__, addr, pc,
               actual_was_hit ? "HIT" : "MISS", predicted_was_hit ? "HIT" : "MISS");
  }
}

bool BloomFilter::predict(Addr pc, Addr addr)
{
  // Check if the predictor is enabled using the base class method
  if (!isEnabled()) {
    return false; // No prediction if the predictor is disabled
  }

  bool is_present = lookup(addr);

  if constexpr (champsim::debug_print) {
    fmt::print("[BLOOM] {}: Predicting for address: 0x{:x} (PC: 0x{:x}) - Predicted as: {}\n", __func__, addr, pc, is_present ? "PRESENT" : "NOT PRESENT");
  }

  lp_stats.record_prediction(is_present);

  // Return the prediction
  return is_present;
}

void BloomFilter::reset()
{
  std::fill(filter.begin(), filter.end(), false);
  entriesSeen = 0;
  if constexpr (champsim::debug_print) {
    fmt::print("[BLOOM] {}: Resetting bloom filter\n", __func__);
  }
}

bool BloomFilter::lookup(Addr instPC) const
{
  uint64_t h1 = hash1(instPC) % filterSize;
  uint64_t h2 = hash2(instPC) % filterSize;
  uint64_t h3 = hash3(instPC) % filterSize;
  uint64_t h4 = hash4(instPC) % filterSize;

  return filter[h1] && filter[h2] && filter[h3] && filter[h4];
}

void BloomFilter::insert(Addr instPC)
{
  if (lookup(instPC)) // Check enabled and if already present
    return;

  uint64_t h1 = hash1(instPC) % filterSize;
  uint64_t h2 = hash2(instPC) % filterSize;
  uint64_t h3 = hash3(instPC) % filterSize;
  uint64_t h4 = hash4(instPC) % filterSize;

  filter[h1] = true;
  filter[h2] = true;
  filter[h3] = true;
  filter[h4] = true;

  entriesSeen++;
  if (entriesSeen > maxEntries)
    reset();
}

uint64_t BloomFilter::hash1(uint64_t key) const
{
  key = (key ^ 0xdeadbeef) + (key << 4);
  key = key ^ (key >> 10);
  key = key + (key << 7);
  key = key ^ (key >> 13);
  return key;
}

uint64_t BloomFilter::hash2(uint64_t key) const
{
  key = (key ^ 61) ^ (key >> 16);
  key = key + (key << 3);
  key = key ^ (key >> 4);
  key = key * 0x27d4eb2d;
  key = key ^ (key >> 15);
  return key;
}

uint64_t BloomFilter::hash3(uint64_t key) const
{
  key = (key ^ 0xc0ffee) + (key << 5);
  key = key ^ (key >> 11);
  key = key * 0x5a17d2d;
  key = key ^ (key >> 17);
  return key;
}

uint64_t BloomFilter::hash4(uint64_t key) const
{
  key = (key ^ 0xdeadcafe) + (key << 6);
  key = key ^ (key >> 9);
  key = key * 0x6d2b79a5;
  key = key ^ (key >> 14);
  return key;
}
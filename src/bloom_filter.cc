#include "bloom_filter.h"

#include "champsim.h"
#include <fmt/core.h>

BloomFilter::BloomFilter(size_t size, size_t maxEntries) : filter(size, false), filterSize(size), entriesSeen(0), maxEntries(maxEntries)
{
  fmt::print("Bloom filter size: {} max entries: {}\n", filterSize, maxEntries);
}

void BloomFilter::setEnabled(bool enabled) { enable = enabled; }

bool BloomFilter::isEnabled() const { return enable; }

bool BloomFilter::lookup(Addr instPC) const
{
  if (!isEnabled())
    return false;

  uint64_t h1 = hash1(instPC) % filterSize;
  uint64_t h2 = hash2(instPC) % filterSize;
  uint64_t h3 = hash3(instPC) % filterSize;
  uint64_t h4 = hash4(instPC) % filterSize;

  return filter[h1] && filter[h2] && filter[h3] && filter[h4];
}

void BloomFilter::insert(Addr instPC)
{
  if (!isEnabled() || lookup(instPC))
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

void BloomFilter::reset()
{
  std::fill(filter.begin(), filter.end(), false);
  entriesSeen = 0;
  if constexpr (champsim::debug_print) {
    fmt::print("[BLOOM] {}: Resetting bloom filter\n", __func__);
  }
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

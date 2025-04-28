#ifndef BLOOM_FILTER_H
#define BLOOM_FILTER_H

#include <cstdint>
#include <set>
#include <vector>

using Addr = uint64_t;

class BloomFilter
{
public:
  BloomFilter(size_t size = 31235, size_t maxEntries = 5000);

  void setEnabled(bool enabled);
  bool isEnabled() const;
  bool lookup(Addr instPC) const;
  void insert(Addr instPC);
  void reset();

private:
  bool enable = false;
  std::vector<bool> filter;
  size_t filterSize;
  size_t maxEntries;
  size_t entriesSeen;

  uint64_t hash1(uint64_t key) const;
  uint64_t hash2(uint64_t key) const;
  uint64_t hash3(uint64_t key) const;
  uint64_t hash4(uint64_t key) const;
};

#endif // BLOOM_FILTER_H

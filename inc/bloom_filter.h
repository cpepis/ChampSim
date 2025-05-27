#ifndef BLOOM_FILTER_H
#define BLOOM_FILTER_H

#include <cstdint>
#include <vector>

#include "load_predictor.h" // Include the base class header

class BloomFilter : public LoadPredictor // Inherit from load_predictor
{
public:
  BloomFilter(LoadPredictorStats& s_lp, size_t size = 31235, size_t maxEntries = 5000); // Constructor
  ~BloomFilter() = default;                                                             // Default destructor

  void update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) override;
  bool predict(Addr pc, Addr addr) override;
  void reset() override;

private:
  std::vector<bool> filter;
  size_t filterSize;
  size_t maxEntries;
  size_t entriesSeen;

  // BloomFilter specific methods
  bool lookup(Addr instPC) const;
  void insert(Addr instPC);

  // Hash functions
  uint64_t hash1(uint64_t key) const;
  uint64_t hash2(uint64_t key) const;
  uint64_t hash3(uint64_t key) const;
  uint64_t hash4(uint64_t key) const;
};

#endif // BLOOM_FILTER_H
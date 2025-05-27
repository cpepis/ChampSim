#ifndef GSKEW_PREDICTOR_H
#define GSKEW_PREDICTOR_H

#include <cstdint>
#include <vector>

#include "load_predictor.h" // Include the base class header

// Define constants for Gskew predictor
constexpr size_t GSKEW_HISTORY_LENGTH = 20;
constexpr size_t GSKEW_TABLE_ENTRIES = 1024; // Each table has 1K entries (2^10)
constexpr size_t GSKEW_NUM_TABLES = 3;       // Three tables for Gskew

class GskewPredictor : public LoadPredictor
{
public:
  GskewPredictor(LoadPredictorStats& s_lp); // Constructor
  ~GskewPredictor() = default;              // Default destructor

  void update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) override;
  bool predict(Addr pc, Addr addr) override;

private:
  // Global History Register (GHR) - stores the last GSKEW_HISTORY_LENGTH load outcomes
  uint32_t global_history_register; // uint32_t can hold 20 bits

  // Three Pattern History Tables (PHTs), each with GSKEW_TABLE_ENTRIES entries
  std::vector<std::vector<uint8_t>> pht; // pht[table_idx][entry_idx]

  // Helper functions for skewed hashing
  // These functions will combine PC and GHR in different ways
  uint64_t hash_func1(Addr addr, uint32_t ghr) const;
  uint64_t hash_func2(Addr addr, uint32_t ghr) const;
  uint64_t hash_func3(Addr addr, uint32_t ghr) const;

  // Helper to update a 2-bit saturating counter
  void update_counter(uint8_t& counter, bool hit);
};

#endif // GSKEW_PREDICTOR_H
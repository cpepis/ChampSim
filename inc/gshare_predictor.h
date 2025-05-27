#ifndef GSHARE_PREDICTOR_H
#define GSHARE_PREDICTOR_H

#include <cstdint>
#include <vector>

#include "load_predictor.h" // Include the base class header

// Define constants for Gshare predictor
constexpr size_t GSHARE_HISTORY_LENGTH = 11;
// The size of the PHT is typically 2^GSHARE_HISTORY_LENGTH if using only GHR for indexing.
// However, since it's a gshare, we XOR PC with GHR. A common approach is to have
// a table size equal to 2^GSHARE_HISTORY_LENGTH and XOR the lower bits of PC.
// Let's assume PHT size is 2^11 entries.
constexpr size_t GSHARE_PHT_ENTRIES = 1 << GSHARE_HISTORY_LENGTH; // 2^11 = 2048 entries

class GsharePredictor : public LoadPredictor
{
public:
  GsharePredictor(LoadPredictorStats& s_lp); // Constructor
  ~GsharePredictor() = default;              // Default destructor

  void update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) override;
  bool predict(Addr pc, Addr addr) override;

private:
  // Global History Register (GHR) - stores the last GSHARE_HISTORY_LENGTH load outcomes
  uint16_t global_history_register; // uint16_t can hold 11 bits

  // Pattern History Table (PHT) - array of 2-bit saturating counters
  std::vector<uint8_t> pht;

  // Helper function to get the index into the PHT
  uint64_t get_pht_index(Addr addr) const;

  // Helper to update a 2-bit saturating counter
  void update_counter(uint8_t& counter, bool hit);
};

#endif // GSHARE_PREDICTOR_H
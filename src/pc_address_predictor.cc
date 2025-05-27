#include "pc_address_predictor.h"

#include "champsim.h"
#include <fmt/core.h>

// Constructor: Initializes the base class and the PHT
PCAddressPredictor::PCAddressPredictor(LoadPredictorStats& s_lp)
    : LoadPredictor(s_lp), pht(PC_ADDR_PHT_ENTRIES, 2) // Initialize all 2-bit counters to 'weakly taken' (binary 10)
{
  // Enable the predictor upon construction
  setEnabled(true);
  fmt::print("PCAddressPredictor initialized with {} entries (2-bit counters).\n", PC_ADDR_PHT_ENTRIES);
}

// Helper function to generate a PHT index
// This is where the "clever" combination of PC and virtual_address happens.
// A simple XOR-based hash is used here. You can experiment with more complex hashes.
uint64_t PCAddressPredictor::get_index(Addr pc, Addr virtual_address) const
{
  // Combine PC and a portion of the virtual address
  // Virtual address is shifted right to get rid of common L1 block offsets (typically 6 bits)
  // Then XORed with PC. The result is then masked/modded by table size.
  uint64_t combined_hash = (pc ^ (virtual_address >> 6)); // Assuming 64-byte blocks for 6-bit shift

  return combined_hash % PC_ADDR_PHT_ENTRIES;
}

void PCAddressPredictor::update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit)
{
  if (!isEnabled()) {
    return; // Cannot update if disabled
  }

  lp_stats.record_outcome(actual_was_hit, predicted_was_hit);

  uint64_t index = get_index(pc, addr);

  // Update the 2-bit saturating counter
  if (actual_was_hit) {
    // Actual HIT: Increment counter (saturate at 3)
    if (pht[index] < 3) {
      pht[index]++;
    }
  } else {
    // Actual MISS: Decrement counter (saturate at 0)
    if (pht[index] > 0) {
      pht[index]--;
    }
  }

  if constexpr (champsim::debug_print) {
    fmt::print("[PCAddr_LP] {}: Updated PC 0x{:x}, Addr 0x{:x}, Actual: {}. Predicted: {}. New Counter: {}\n", __func__, pc, addr, actual_was_hit,
               predicted_was_hit, pht[index]);
  }
}

bool PCAddressPredictor::predict(Addr pc, Addr addr)
{
  if (!isEnabled()) {
    return false; // Cannot predict if disabled
  }

  uint64_t index = get_index(pc, addr);
  bool prediction = (pht[index] >= 2); // Predict hit if counter is 2 or 3 (weakly taken, strongly taken)

  if constexpr (champsim::debug_print) {
    fmt::print("[PCAddr_LP] {}: PC 0x{:x}, Addr 0x{:x} (idx {}). Counter: {} -> Pred: {}\n", __func__, pc, addr, index, pht[index],
               prediction ? "HIT" : "MISS");
  }

  lp_stats.record_prediction(prediction);

  return prediction;
}
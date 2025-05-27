#include "gshare_predictor.h"

#include "champsim.h"
#include <fmt/core.h>

GsharePredictor::GsharePredictor(LoadPredictorStats& s_lp)
    : LoadPredictor(s_lp),        // Call base class constructor
      global_history_register(0), // Initialize GHR to 0
      pht(GSHARE_PHT_ENTRIES, 2)  // Initialize 2-bit counters to weakly taken (2, out of 0-3)
{
  // Enable the predictor upon construction
  setEnabled(true);

  fmt::print("GsharePredictor initialized with {} PHT entries and {} history length.\n", GSHARE_PHT_ENTRIES, GSHARE_HISTORY_LENGTH);
}

// Helper to get the index into the PHT using PC XOR GHR
uint64_t GsharePredictor::get_pht_index(Addr addr) const
{
  // A common way to get PC bits is to shift right to ignore alignment bits
  // and then take the lower GSHARE_HISTORY_LENGTH bits.
  // For simplicity, let's assume `addr` is already suitable or use lower bits.
  // Assuming PC is word-aligned, we can shift right by 2 for example.
  uint64_t pc_hash = (addr >> 2) & (GSHARE_PHT_ENTRIES - 1); // Use lower bits of PC

  // Gshare: XOR PC bits with GHR
  return (pc_hash ^ global_history_register) % GSHARE_PHT_ENTRIES;
}

// Helper to update a 2-bit saturating counter
void GsharePredictor::update_counter(uint8_t& counter, bool hit)
{
  if (hit) {
    if (counter < 3) { // Max value for 2-bit counter is 3 (binary 11)
      counter++;
    }
  } else {
    if (counter > 0) { // Min value for 2-bit counter is 0 (binary 00)
      counter--;
    }
  }
}

void GsharePredictor::update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit)
{
  if (!isEnabled()) {
    return;
  }

  lp_stats.record_outcome(actual_was_hit, predicted_was_hit);

  uint64_t pht_index = get_pht_index(addr);

  // Update the 2-bit saturating counter at the calculated index
  update_counter(pht[pht_index], actual_was_hit);

  // Update the Global History Register (GHR)
  // Shift left and add the new outcome bit
  global_history_register = (global_history_register << 1) | (actual_was_hit ? 1 : 0);
  // Mask to keep only the last GSHARE_HISTORY_LENGTH bits
  global_history_register &= ((1 << GSHARE_HISTORY_LENGTH) - 1);

  if constexpr (champsim::debug_print) {
    fmt::print("[GSHARE_LP] {}: Updating for address 0x{:x}, PHT index {}. GHR: {:0{}b}, PHT: {} -> {}, Actual: {}, Predicted: {}\n", __func__, addr, pht_index,
               global_history_register, GSHARE_HISTORY_LENGTH, pht[pht_index], actual_was_hit ? "HIT" : "MISS", actual_was_hit, predicted_was_hit);
  }
}

bool GsharePredictor::predict(Addr pc, Addr addr)
{
  if (!isEnabled()) {
    return false; // Cannot predict if disabled
  }

  uint64_t pht_index = get_pht_index(addr);

  // Prediction is based on the state of the 2-bit saturating counter
  bool prediction = pht[pht_index] >= 2; // Predict hit if counter is 2 (weakly taken) or 3 (strongly taken)

  if constexpr (champsim::debug_print) {
    fmt::print("[GSHARE_LP] {}: Predicting for address 0x{:x}, PHT index {}. GHR: {:0{}b}, PHT: {}, Prediction: {}\n", __func__, addr, pht_index,
               global_history_register, GSHARE_HISTORY_LENGTH, pht[pht_index], prediction ? "HIT" : "MISS");
  }

  lp_stats.record_prediction(prediction);

  return prediction;
}
#include "gskew_predictor.h"

#include "champsim.h"
#include <fmt/core.h>

GskewPredictor::GskewPredictor(LoadPredictorStats& s_lp)
    : LoadPredictor(s_lp),       // Call base class constructor
      global_history_register(0) // Initialize GHR to 0
{
  // Initialize the three PHTs
  pht.resize(GSKEW_NUM_TABLES);
  for (size_t i = 0; i < GSKEW_NUM_TABLES; ++i) {
    pht[i].resize(GSKEW_TABLE_ENTRIES, 2); // Initialize 2-bit counters to weakly taken
  }

  // Enable the predictor upon construction
  setEnabled(true);

  fmt::print("GskewPredictor initialized with {} tables, each with {} entries, and {} history length.\n", GSKEW_NUM_TABLES, GSKEW_TABLE_ENTRIES,
             GSKEW_HISTORY_LENGTH);
}

// Helper to update a 2-bit saturating counter
void GskewPredictor::update_counter(uint8_t& counter, bool hit)
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

// Skewed hash function 1 (example: simple XOR)
uint64_t GskewPredictor::hash_func1(Addr addr, uint32_t ghr) const
{
  // Use lower bits of PC (e.g., bits 2-11 for 1K table, assuming 4-byte alignment)
  uint64_t pc_lower_bits = (addr >> 2) & (GSKEW_TABLE_ENTRIES - 1);
  // XOR with a portion of GHR. Need to ensure GHR portion matches table size.
  // If table size is 2^10, use lower 10 bits of GHR.
  uint64_t ghr_lower_bits = ghr & (GSKEW_TABLE_ENTRIES - 1);
  return (pc_lower_bits ^ ghr_lower_bits) % GSKEW_TABLE_ENTRIES;
}

// Skewed hash function 2 (example: XOR with a different portion of GHR or different shift)
uint64_t GskewPredictor::hash_func2(Addr addr, uint32_t ghr) const
{
  uint64_t pc_middle_bits = (addr >> 12) & (GSKEW_TABLE_ENTRIES - 1); // Use higher PC bits
  uint64_t ghr_shifted_bits = (ghr >> 1) & (GSKEW_TABLE_ENTRIES - 1); // Shift GHR
  return (pc_middle_bits ^ ghr_shifted_bits) % GSKEW_TABLE_ENTRIES;
}

// Skewed hash function 3 (example: XOR with yet another portion or more complex mix)
uint64_t GskewPredictor::hash_func3(Addr addr, uint32_t ghr) const
{
  uint64_t pc_high_bits = (addr >> 22) & (GSKEW_TABLE_ENTRIES - 1);                   // Use even higher PC bits
  uint64_t ghr_xor_pc_low = (ghr ^ ((addr >> 2) & 0x1F)) & (GSKEW_TABLE_ENTRIES - 1); // XOR GHR with some PC bits
  return (pc_high_bits ^ ghr_xor_pc_low) % GSKEW_TABLE_ENTRIES;
}

void GskewPredictor::update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit)
{
  if (!isEnabled()) {
    return;
  }

  lp_stats.record_outcome(actual_was_hit, predicted_was_hit);

  // Get indices for all three tables
  uint64_t index1 = hash_func1(addr, global_history_register);
  uint64_t index2 = hash_func2(addr, global_history_register);
  uint64_t index3 = hash_func3(addr, global_history_register);

  // Update the 2-bit saturating counters in all three tables
  update_counter(pht[0][index1], actual_was_hit);
  update_counter(pht[1][index2], actual_was_hit);
  update_counter(pht[2][index3], actual_was_hit);

  // Update the Global History Register (GHR)
  global_history_register = (global_history_register << 1) | (actual_was_hit ? 1 : 0);
  // Mask to keep only the last GSKEW_HISTORY_LENGTH bits
  global_history_register &= ((1 << GSKEW_HISTORY_LENGTH) - 1);

  if constexpr (champsim::debug_print) {
    fmt::print("[GSKEW_LP] {}: Updating for address 0x{:x}, GHR: {:0{}b}\n", __func__, addr, global_history_register, GSKEW_HISTORY_LENGTH);
    fmt::print("  Actual: {}, Predicted: {}\n", actual_was_hit ? "HIT" : "MISS", predicted_was_hit ? "HIT" : "MISS");
    fmt::print("  PHT[0][{}]={}, PHT[1][{}]={}, PHT[2][{}]={}\n", index1, pht[0][index1], index2, pht[1][index2], index3, pht[2][index3]);
  }
}

bool GskewPredictor::predict(Addr pc, Addr addr)
{
  if (!isEnabled()) {
    return false; // Cannot predict if disabled
  }

  // Get indices for all three tables
  uint64_t index1 = hash_func1(addr, global_history_register);
  uint64_t index2 = hash_func2(addr, global_history_register);
  uint64_t index3 = hash_func3(addr, global_history_register);

  // Get predictions from each table
  bool pred1 = pht[0][index1] >= 2;
  bool pred2 = pht[1][index2] >= 2;
  bool pred3 = pht[2][index3] >= 2;

  // The Gskew uses a simple majority vote internally for its own prediction
  int true_votes = 0;
  if (pred1)
    true_votes++;
  if (pred2)
    true_votes++;
  if (pred3)
    true_votes++;

  bool final_prediction = (true_votes >= 2); // Predict hit if at least 2 out of 3 predict hit

  if constexpr (champsim::debug_print) {
    fmt::print("[GSKEW_LP] {}: Predicting for address 0x{:x}. GHR: {:0{}b}\n", __func__, addr, global_history_register, GSKEW_HISTORY_LENGTH);
    fmt::print("  Preds: T1={}, T2={}, T3={}. Final: {}\n", pred1, pred2, pred3, final_prediction ? "HIT" : "MISS");
  }

  lp_stats.record_prediction(final_prediction);

  return final_prediction;
}
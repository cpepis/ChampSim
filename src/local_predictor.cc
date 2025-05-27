#include "local_predictor.h"

#include "champsim.h"
#include <fmt/core.h>

LocalPredictor::LocalPredictor(LoadPredictorStats& s_lp) : LoadPredictor(s_lp), history_table(LOCAL_TABLE_ENTRIES, 0), pht(LOCAL_TABLE_ENTRIES, 2)
{
  setEnabled(true);
  fmt::print("LocalPredictor initialized with {} entries and {} history length.\n", LOCAL_TABLE_ENTRIES, LOCAL_HISTORY_LENGTH);
}

uint64_t LocalPredictor::get_index(Addr addr) const { return (addr ^ (addr >> 10)) % LOCAL_TABLE_ENTRIES; }

void LocalPredictor::update_counter(uint8_t& counter, bool hit)
{
  if (hit) {
    if (counter < 3) {
      counter++;
    }
  } else {
    if (counter > 0) {
      counter--;
    }
  }
}

void LocalPredictor::update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit)
{
  if (!isEnabled()) {
    return;
  }

  lp_stats.record_outcome(actual_was_hit, predicted_was_hit);

  uint64_t index = get_index(addr);

  // Update the history: Shift left and add the new outcome
  history_table[index] = (history_table[index] << 1) | (actual_was_hit ? 1 : 0);
  // Mask to keep only the last LOCAL_HISTORY_LENGTH bits
  history_table[index] &= ((1 << LOCAL_HISTORY_LENGTH) - 1);

  // Update the 2-bit saturating counter
  update_counter(pht[index], actual_was_hit);

  if constexpr (champsim::debug_print) {
    fmt::print("[LOCAL_LP] {}: Updating for address 0x{:x}, index {}. Actual: {}, Predicted: {}, History: {:08b}, PHT: {}\n", __func__, addr, index,
               actual_was_hit ? "HIT" : "MISS", predicted_was_hit ? "HIT" : "MISS", history_table[index], pht[index]);
  }
}

bool LocalPredictor::predict(Addr pc, Addr addr)
{
  if (!isEnabled()) {
    return false;
  }

  uint64_t index = get_index(addr);

  uint8_t current_history = history_table[index]; // For debug printing, not strictly used for prediction here

  bool prediction = pht[index] >= 2; // Predict hit if counter is 2 (weakly taken) or 3 (strongly taken)

  if constexpr (champsim::debug_print) {
    fmt::print("[LOCAL_LP] {}: Predicting for address 0x{:x}, index {}. Current history: {:08b}, PHT: {}, Prediction: {}\n", __func__, addr, index,
               current_history, pht[index], prediction ? "HIT" : "MISS");
  }

  lp_stats.record_prediction(prediction);

  return prediction;
}
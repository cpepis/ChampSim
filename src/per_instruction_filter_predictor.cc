#include "per_instruction_filter_predictor.h"

#include <algorithm>

#include "champsim.h"
#include <fmt/core.h>

PerInstructionFilterPredictor::PerInstructionFilterPredictor(LoadPredictorStats& s_lp, GlobalCounterPredictor* global_ctr_predictor)
    : LoadPredictor(s_lp),                                  // Call base class constructor
      pif_counters(PIF_TABLE_ENTRIES, PIF_COUNTER_MAX / 2), // Initialize counters to a neutral state (e.g., 1 or 2)
      pif_silenced_bits(PIF_TABLE_ENTRIES, false),          // All counters initially not silenced
      global_counter_fallback(global_ctr_predictor),        // Store pointer to fallback predictor
      committed_loads_since_last_reset(0)
{
  // Enable the predictor upon construction
  setEnabled(true);

  // Ensure the fallback predictor is valid
  if (global_counter_fallback == nullptr) {
    fmt::print(stderr, "Error: GlobalCounterPredictor fallback pointer is null!\n");
    // You might want to throw an exception or handle this more robustly
  }

  fmt::print("PerInstructionFilterPredictor initialized with {} entries. Fallback: {}\n", PIF_TABLE_ENTRIES,
             (global_counter_fallback != nullptr) ? "Enabled" : "Disabled/Invalid");
}

// Helper to get the direct-mapped index (lower bits of PC)
uint64_t PerInstructionFilterPredictor::get_index(Addr addr) const
{
  return (addr >> 2) % PIF_TABLE_ENTRIES; // Assuming 4-byte aligned addresses
}

// Helper to determine if a state is saturated (00 or 11)
bool PerInstructionFilterPredictor::is_saturated(uint8_t counter_value) const
{
  return (counter_value == PIF_COUNTER_MIN) || (counter_value == PIF_COUNTER_MAX);
}

void PerInstructionFilterPredictor::update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit)
{
  if (!isEnabled()) {
    return;
  }

  lp_stats.record_outcome(actual_was_hit, predicted_was_hit);

  uint64_t index = get_index(addr);
  uint8_t& counter = pif_counters[index];

  // Read the current silenced state directly from the vector element
  bool current_silenced_state = pif_silenced_bits[index];

  // Only update if the counter is NOT silenced
  if (!current_silenced_state) {
    uint8_t old_counter_value = counter;
    bool old_saturated = is_saturated(old_counter_value);

    // Update the 2-bit counter
    if (actual_was_hit) {
      if (counter < PIF_COUNTER_MAX) {
        counter++;
      }
    } else { // was a miss
      if (counter > PIF_COUNTER_MIN) {
        counter--;
      }
    }

    // Check for silencing condition: from saturated to transient
    if (old_saturated && !is_saturated(counter)) {
      pif_silenced_bits[index] = true; // Directly assign to the vector element
      if constexpr (champsim::debug_print) {
        fmt::print("[PIF_LP] {}: Address 0x{:x} (idx {}). Counter from {} to {}. SILENCED.\n", __func__, addr, index, old_counter_value, counter);
      }
    }
  }

  // Increment committed loads counter for silent bit reset logic
  committed_loads_since_last_reset++;
  if (committed_loads_since_last_reset >= PIF_SILENCE_RESET_INTERVAL) {
    std::fill(pif_silenced_bits.begin(), pif_silenced_bits.end(), false); // Reset all silent bits
    committed_loads_since_last_reset = 0;
    if constexpr (champsim::debug_print) {
      fmt::print("[PIF_LP] {}: Resetting all silent bits after {} committed loads.\n", __func__, PIF_SILENCE_RESET_INTERVAL);
    }
  }

  if constexpr (champsim::debug_print) {
    // Use pif_silenced_bits[index] directly for printing
    fmt::print("[PIF_LP] {}: Address 0x{:x} (idx {}). Updated counter: {}. Silenced: {}\n", __func__, addr, index, counter,
               pif_silenced_bits[index] ? "YES" : "NO");
  }
}

bool PerInstructionFilterPredictor::predict(Addr pc, Addr addr)
{
  if (!isEnabled()) {
    return false; // Cannot predict if disabled
  }

  uint64_t index = get_index(addr);
  uint8_t counter_value = pif_counters[index];
  bool silenced = pif_silenced_bits[index]; // This read is fine

  bool final_prediction;

  if (!silenced) {
    // Counter is NOT silenced: Predict based on its state
    final_prediction = (counter_value >= (PIF_COUNTER_MAX / 2 + 1));
    if constexpr (champsim::debug_print) {
      fmt::print("[PIF_LP] {}: Address 0x{:x} (idx {}). NOT silenced. Counter: {} -> Pred: {}\n", __func__, addr, index, counter_value,
                 final_prediction ? "HIT" : "MISS");
    }
  } else {
    // Counter IS silenced: Let the Global Counter predictor decide
    if (global_counter_fallback != nullptr) {
      final_prediction = global_counter_fallback->predict(pc, addr);
      if constexpr (champsim::debug_print) {
        fmt::print("[PIF_LP] {}: Address 0x{:x} (idx {}). SILENCED. Fallback to Global Counter -> Pred: {}\n", __func__, addr, index,
                   final_prediction ? "HIT" : "MISS");
      }
    } else {
      // Fallback predictor is not available, default to a safe prediction (e.g., miss)
      final_prediction = false;
      if constexpr (champsim::debug_print) {
        fmt::print("[PIF_LP] {}: Address 0x{:x} (idx {}). SILENCED but no fallback. Default to MISS.\n", __func__, addr, index);
      }
    }
  }

  lp_stats.record_prediction(final_prediction);

  return final_prediction;
}
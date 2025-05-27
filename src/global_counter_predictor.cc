#include "global_counter_predictor.h"

#include "champsim.h"
#include <fmt/core.h>

GlobalCounterPredictor::GlobalCounterPredictor(LoadPredictorStats& s_lp)
    : LoadPredictor(s_lp),                             // Call base class constructor
      global_scheduling_counter(COUNTER_MAX_VALUE / 2) // Initialize to a neutral/mildly aggressive state, e.g., 7 or 8.
{
  // Enable the predictor upon construction
  setEnabled(true);

  fmt::print("GlobalCounterPredictor initialized. Counter value: {}\n", global_scheduling_counter);
}

// Helper to update the global counter with saturation logic
void GlobalCounterPredictor::update_counter_value(int8_t delta)
{
  int16_t new_value = static_cast<int16_t>(global_scheduling_counter) + delta;

  if (new_value > COUNTER_MAX_VALUE) {
    global_scheduling_counter = COUNTER_MAX_VALUE;
  } else if (new_value < COUNTER_MIN_VALUE) {
    global_scheduling_counter = COUNTER_MIN_VALUE;
  } else {
    global_scheduling_counter = static_cast<uint8_t>(new_value);
  }
}

void GlobalCounterPredictor::update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit)
{
  if (!isEnabled()) {
    return;
  }

  lp_stats.record_outcome(actual_was_hit, predicted_was_hit);

  bool was_l1_miss = !actual_was_hit; // L1 miss means 'was_hit' is false

  if (was_l1_miss) {
    // Decrement by two on cycles where an L1 miss takes place
    update_counter_value(-2);
  } else {
    // Increment by one otherwise (L1 hit or other cycles)
    update_counter_value(1);
  }

  if constexpr (champsim::debug_print) {
    fmt::print("[GLOBAL_CTR_LP] {}: Updating for addr 0x{:x}. Counter: {} -> New counter value: {}\n", __func__, addr, global_scheduling_counter,
               static_cast<int>(global_scheduling_counter));
  }
}

bool GlobalCounterPredictor::predict(Addr pc, Addr addr)
{
  if (!isEnabled()) {
    return false; // Cannot predict if disabled
  }

  // The most significant bit (MSB) of a 4-bit counter (0-15)
  // The 4-bit values are:
  // 0000 (0) - 0111 (7): MSB is 0
  // 1000 (8) - 1111 (15): MSB is 1
  // So, if counter >= 8, MSB is 1; otherwise, MSB is 0.
  bool should_aggressively_schedule = (global_scheduling_counter >= (COUNTER_MAX_VALUE / 2 + 1));
  // For a 4-bit counter (0-15), MSB is bit 3 (if bits are 0-indexed).
  // (global_scheduling_counter & 0b1000) or (global_scheduling_counter & 0x8)
  // This is equivalent to global_scheduling_counter >= 8

  if constexpr (champsim::debug_print) {
    fmt::print("[GLOBAL_CTR_LP] {}: Predicting for addr 0x{:x}. Counter: {} -> Should aggressively schedule: {}\n", __func__, addr, global_scheduling_counter,
               should_aggressively_schedule);
  }

  lp_stats.record_prediction(should_aggressively_schedule);

  return should_aggressively_schedule;
}
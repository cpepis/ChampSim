#ifndef GLOBAL_COUNTER_PREDICTOR_H
#define GLOBAL_COUNTER_PREDICTOR_H

#include <cstdint>

#include "load_predictor.h" // Include the base class header

class GlobalCounterPredictor : public LoadPredictor
{
public:
  GlobalCounterPredictor(LoadPredictorStats& s_lp); // Constructor
  ~GlobalCounterPredictor() = default;              // Default destructor

  void update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) override;
  bool predict(Addr pc, Addr addr) override;

private:
  // A 4-bit saturating counter (can be represented by uint8_t)
  // The range for a 4-bit counter is 0 to 15.
  uint8_t global_scheduling_counter;

  // Constants for counter operations
  static constexpr uint8_t COUNTER_MAX_VALUE = 15; // 2^4 - 1
  static constexpr uint8_t COUNTER_MIN_VALUE = 0;

  // Helper to update the counter with saturation logic
  void update_counter_value(int8_t delta);
};

#endif // GLOBAL_COUNTER_PREDICTOR_H
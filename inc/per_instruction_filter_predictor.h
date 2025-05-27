#ifndef PER_INSTRUCTION_FILTER_PREDICTOR_H
#define PER_INSTRUCTION_FILTER_PREDICTOR_H

#include <cstdint>
#include <vector>

#include "global_counter_predictor.h" // Needed for fallback prediction
#include "load_predictor.h"           // Base class

// Define constants for the per-instruction filter
constexpr size_t PIF_TABLE_ENTRIES = 2048;             // 2K-entry direct-mapped
constexpr uint8_t PIF_COUNTER_MAX = 3;                 // 2-bit counter max value
constexpr uint8_t PIF_COUNTER_MIN = 0;                 // 2-bit counter min value
constexpr uint64_t PIF_SILENCE_RESET_INTERVAL = 10000; // Reset silent bits every 10K committed loads

class PerInstructionFilterPredictor : public LoadPredictor
{
public:
  PerInstructionFilterPredictor(LoadPredictorStats& s_lp, GlobalCounterPredictor* global_ctr_predictor); // Constructor
  ~PerInstructionFilterPredictor() = default;                                                            // Default destructor

  void update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) override;
  bool predict(Addr pc, Addr addr) override;

private:
  // Direct-mapped array of 2-bit saturating counters
  std::vector<uint8_t> pif_counters;
  // Additional bit to allow silencing the counter
  std::vector<bool> pif_silenced_bits;

  // Pointer to the Global Counter Load Predictor for fallback
  GlobalCounterPredictor* global_counter_fallback;

  uint64_t committed_loads_since_last_reset;

  // Helper to get the direct-mapped index
  uint64_t get_index(Addr addr) const;

  // Helper to determine if a state is saturated (00 or 11)
  bool is_saturated(uint8_t counter_value) const;
};

#endif // PER_INSTRUCTION_FILTER_PREDICTOR_H
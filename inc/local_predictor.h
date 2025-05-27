#ifndef LOCAL_PREDICTOR_H
#define LOCAL_PREDICTOR_H

#include <cstdint>
#include <vector>

#include "load_predictor.h" // Include the base class header

constexpr size_t LOCAL_HISTORY_LENGTH = 8;
constexpr size_t LOCAL_TABLE_ENTRIES = 2048;

class LocalPredictor : public LoadPredictor
{
public:
  LocalPredictor(LoadPredictorStats& s_lp); // Constructor
  ~LocalPredictor() = default;              // Default destructor

  void update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) override;
  bool predict(Addr pc, Addr addr) override;

private:
  std::vector<uint8_t> history_table; // Each entry stores an 8-bit history
  std::vector<uint8_t> pht;           // Pattern History Table (e.g., 2-bit saturating counters)

  uint64_t get_index(Addr addr) const;
  void update_counter(uint8_t& counter, bool hit);
};

#endif // LOCAL_PREDICTOR_H
#ifndef PC_ADDRESS_PREDICTOR_H
#define PC_ADDRESS_PREDICTOR_H

#include <cstdint>
#include <vector>

#include "load_predictor.h" // Includes cpu_stats.h and load_predictor_stats.h

// Define a reasonable size for the Pattern History Table (PHT)
// This can be tuned for performance/accuracy. A power of 2 is good for hashing.
static constexpr size_t PC_ADDR_PHT_ENTRIES = 4096; // Example: 4K entries

class PCAddressPredictor : public LoadPredictor
{
public:
  PCAddressPredictor(LoadPredictorStats& s_lp); // Constructor
  ~PCAddressPredictor() = default;              // Default destructor

  void update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) override;
  bool predict(Addr pc, Addr addr) override;

private:
  // Pattern History Table (PHT) of 2-bit saturating counters
  // Each entry represents a prediction for a (PC, Address) pair
  std::vector<uint8_t> pht;

  // Helper function to generate an index into the PHT from PC and virtual_address
  uint64_t get_index(Addr pc, Addr virtual_address) const;
};

#endif // PC_ADDRESS_PREDICTOR_H
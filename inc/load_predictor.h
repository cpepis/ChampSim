#ifndef LOAD_PREDICTOR_H
#define LOAD_PREDICTOR_H

#include <cstdint> // For Addr
#include <string>

#include "load_predictor_stats.h"

using Addr = uint64_t;

class LoadPredictor
{
public:
  LoadPredictor(LoadPredictorStats& s_lp) : enabled(false), lp_stats(s_lp) {} // Constructor to initialize enabled state
  virtual ~LoadPredictor() = default;                                         // Virtual destructor for proper cleanup

  // Set whether the load predictor is enabled or disabled
  void setEnabled(bool enabled);

  // Check if the load predictor is currently enabled
  bool isEnabled() const;

  // Update the predictor's internal state based on a load address
  // Default implementation, can be overridden by derived classes
  virtual void update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) = 0;

  // Predict the outcome for a given load address
  // Default implementation, can be overridden by derived classes
  virtual bool predict(Addr pc, Addr addr) = 0;

  // Reset the predictor's internal state
  virtual void reset();

protected:
  bool enabled; // Internal state to track if the predictor is enabled
  LoadPredictorStats& lp_stats;
};

LoadPredictor* create_predictor(const std::string& name, LoadPredictorStats& lp_stats);

#endif // LOAD_PREDICTOR_H
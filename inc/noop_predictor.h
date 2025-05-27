#ifndef NOOP_PREDICTOR_H
#define NOOP_PREDICTOR_H

#include "load_predictor.h" // Include the base class header

class NoopPredictor : public LoadPredictor
{
public:
  NoopPredictor(LoadPredictorStats& s_lp); // Constructor
  ~NoopPredictor() = default;              // Default destructor

  void update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) override;
  bool predict(Addr pc, Addr addr) override;
};

#endif // NOOP_PREDICTOR_H
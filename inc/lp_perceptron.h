#ifndef LP_PERCEPTRON_PREDICTOR_H
#define LP_PERCEPTRON_PREDICTOR_H

#include <cstdint>
#include <vector>

#include "load_predictor.h" // Include the base class header

class PerceptronPredictor : public LoadPredictor
{
public:
  PerceptronPredictor(LoadPredictorStats& s_lp); // Constructor
  ~PerceptronPredictor() = default;              // Default destructor

  void update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) override;
  bool predict(Addr pc, Addr addr) override;
};

#endif // LP_PERCEPTRON_PREDICTOR_H
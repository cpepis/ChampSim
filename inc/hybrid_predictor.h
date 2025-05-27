#ifndef HYBRID_PREDICTOR_H
#define HYBRID_PREDICTOR_H

#include "gshare_predictor.h"
#include "gskew_predictor.h"
#include "load_predictor.h"
#include "local_predictor.h"

class HybridPredictor : public LoadPredictor
{
public:
  HybridPredictor(LoadPredictorStats& s_lp); // Constructor
  ~HybridPredictor();                        // Destructor

  void update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) override;
  bool predict(Addr pc, Addr addr) override;

private:
  // Pointers to the constituent predictors
  LocalPredictor* local_predictor;
  GsharePredictor* gshare_predictor;
  GskewPredictor* gskew_predictor;
};

#endif // HYBRID_PREDICTOR_H
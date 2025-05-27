#include "hybrid_predictor.h"

#include "champsim.h"
#include <fmt/core.h>

HybridPredictor::HybridPredictor(LoadPredictorStats& s_lp) : LoadPredictor(s_lp) // Call base class constructor
{
  // Enable the hybrid predictor itself upon construction
  setEnabled(true);

  // Instantiate all constituent predictors
  local_predictor = new LocalPredictor(s_lp);
  gshare_predictor = new GsharePredictor(s_lp);
  gskew_predictor = new GskewPredictor(s_lp);

  // The individual predictors are already set to enabled in their own constructors.
  // If you ever needed to explicitly disable/enable them here, you could:
  // local_predictor->setEnabled(true);
  // etc.

  fmt::print("HybridPredictor initialized. Components: Local, Gshare, Gskew.\n");
}

HybridPredictor::~HybridPredictor()
{
  // Clean up dynamically allocated memory for sub-predictors
  delete local_predictor;
  delete gshare_predictor;
  delete gskew_predictor;

  if constexpr (champsim::debug_print) {
    fmt::print("HybridPredictor destroyed.\n");
  }
}

void HybridPredictor::update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit)
{
  if (!isEnabled()) {
    return; // No update if the hybrid predictor is disabled
  }

  lp_stats.record_outcome(actual_was_hit, predicted_was_hit);

  // Delegate the update call to all constituent predictors
  // Each sub-predictor learns independently from the actual_was_hit outcome
  local_predictor->update(pc, addr, actual_was_hit, predicted_was_hit);
  gshare_predictor->update(pc, addr, actual_was_hit, predicted_was_hit);
  gskew_predictor->update(pc, addr, actual_was_hit, predicted_was_hit);

  // If there was a more complex chooser mechanism (e.g., meta-predictor with its own PHTs),
  // its state would also be updated here based on which component predicted correctly/incorrectly.
  // For a simple majority vote, no additional state update is explicitly needed here.

  if constexpr (champsim::debug_print) {
    fmt::print("[HYBRID_LP] {}: Updating for address 0x{:x}. Actual: {}, Predicted: {}\n", __func__, addr, actual_was_hit ? "HIT" : "MISS",
               predicted_was_hit ? "HIT" : "MISS");
  }
}

bool HybridPredictor::predict(Addr pc, Addr addr)
{
  if (!isEnabled()) {
    return false; // Cannot predict if the hybrid predictor is disabled
  }

  // Get predictions from all constituent predictors
  bool local_pred = local_predictor->predict(pc, addr);
  bool gshare_pred = gshare_predictor->predict(pc, addr);
  bool gskew_pred = gskew_predictor->predict(pc, addr);

  // Implement the majority vote mechanism
  int true_votes = 0;
  if (local_pred) {
    true_votes++;
  }
  if (gshare_pred) {
    true_votes++;
  }
  if (gskew_pred) {
    true_votes++;
  }

  // The final prediction is "true" (hit) if at least 2 out of 3 predictors voted "true"
  bool final_prediction = (true_votes >= 2);

  if constexpr (champsim::debug_print) {
    fmt::print("[HYBRID_LP] {}: Address 0x{:x}. Local Pred: {}, Gshare Pred: {}, Gskew Pred: {}. Final Prediction: {}\n", __func__, addr, local_pred,
               gshare_pred, gskew_pred, final_prediction ? "HIT" : "MISS");
  }

  lp_stats.record_prediction(final_prediction);

  return final_prediction;
}
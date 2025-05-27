#ifndef LOAD_PREDICTOR_STATS_H
#define LOAD_PREDICTOR_STATS_H

#include <cstdint> // For uint64_t

struct LoadPredictorStats {
  // General Prediction Metrics
  uint64_t total_predictions = 0; // Total times the predictor was queried
  uint64_t predicted_hits = 0;    // How many times the predictor said "HIT" (or "aggressive")
  uint64_t predicted_misses = 0;  // How many times the predictor said "MISS" (or "non-aggressive")

  uint64_t correct_predictions = 0;   // Times the prediction matched the actual outcome
  uint64_t incorrect_predictions = 0; // Times the prediction did NOT match the actual outcome

  // Misprediction Breakdown (Confusion Matrix)
  uint64_t true_positives = 0;  // Predicted HIT, Actual HIT (correctly predicted hit)
  uint64_t true_negatives = 0;  // Predicted MISS, Actual MISS (correctly predicted miss)
  uint64_t false_positives = 0; // Predicted HIT, Actual MISS (MISPREDICTED HIT or OVERPREDICTION)
  uint64_t false_negatives = 0; // Predicted MISS, Actual HIT (MISPREDICTED MISS or UNDERPREDICTION)

  void record_prediction(bool prediction)
  {
    total_predictions++;
    if (prediction) {
      predicted_hits++;
    } else {
      predicted_misses++;
    }
  }

  void record_outcome(bool actual_was_hit, bool predicted_was_hit)
  {
    if (actual_was_hit == predicted_was_hit) {
      correct_predictions++;
    } else {
      incorrect_predictions++;
    }

    if (predicted_was_hit && actual_was_hit) {
      true_positives++;
    } else if (!predicted_was_hit && !actual_was_hit) {
      true_negatives++;
    } else if (predicted_was_hit && !actual_was_hit) {
      false_positives++;
    } else { // !predicted_was_hit && actual_was_hit
      false_negatives++;
    }
  }
};

#endif // LOAD_PREDICTOR_STATS_H
#include "noop_predictor.h"

#include "champsim.h"

NoopPredictor::NoopPredictor(LoadPredictorStats& s_lp) : LoadPredictor(s_lp) {}

void NoopPredictor::update(Addr pc, Addr addr, bool actual_was_hit, bool predicted_was_hit) {}

bool NoopPredictor::predict(Addr pc, Addr addr) { return false; }
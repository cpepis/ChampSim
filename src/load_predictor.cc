#include "load_predictor.h"

#include <stdexcept>

#include "bloom_filter.h"
#include "champsim.h"
#include "global_counter_predictor.h"
#include "gshare_predictor.h"
#include "gskew_predictor.h"
#include "hybrid_predictor.h"
#include "local_predictor.h"
#include "noop_predictor.h"
#include "ooo_cpu.h"
#include "pc_address_predictor.h"
#include "per_instruction_filter_predictor.h"

void LoadPredictor::setEnabled(bool enable) { enabled = enable; }

bool LoadPredictor::isEnabled() const { return enabled; }

void LoadPredictor::reset() {}

LoadPredictor* create_predictor(const std::string& name, LoadPredictorStats& lp_stats)
{
  if (name == "bloomfilter")
    return new BloomFilter(lp_stats);
  else if (name == "globalcounter")
    return new GlobalCounterPredictor(lp_stats);
  else if (name == "gshare")
    return new GsharePredictor(lp_stats);
  else if (name == "gskew")
    return new GskewPredictor(lp_stats);
  else if (name == "hybrid")
    return new HybridPredictor(lp_stats);
  else if (name == "local")
    return new LocalPredictor(lp_stats);
  else if (name == "pap")
    return new PCAddressPredictor(lp_stats);
  else if (name == "pif") {
    static GlobalCounterPredictor* global_ctr_singleton = nullptr;

    if (global_ctr_singleton == nullptr) {
      global_ctr_singleton = new GlobalCounterPredictor(lp_stats);
    }

    return new PerInstructionFilterPredictor(lp_stats, global_ctr_singleton);
  } else if (name == "" || name == "none")
    return new NoopPredictor(lp_stats);
  else
    throw std::invalid_argument("Unknown predictor type: " + name + ". Valid options are: local, gshare, gskew, hybrid, bloomfilter.");
}
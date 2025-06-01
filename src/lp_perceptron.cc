/*
 * Copyright (c) 2001 University of Texas at Austin
 *
 * Daniel A. Jimenez
 * Calvin Lin
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software (the "Software"), to deal in
 * the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE UNIVERSITY OF TEXAS AT
 * AUSTIN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file implements the simulated perceptron branch predictor from:
 *
 * Jimenez, D. A. & Lin, C., Dynamic branch prediction with perceptrons,
 * Proceedings of the Seventh International Symposium on High Performance
 * Computer Architecture (HPCA), Monterrey, NL, Mexico 2001
 *
 * The #define's here specify a perceptron predictor with a history
 * length of 24, 163 perceptrons, and  8-bit weights.  This represents
 * a hardware budget of (24+1)*8*163 = 32600 bits, or about 4K bytes,
 * which is comparable to the hardware budget of the Alpha 21264 hybrid
 * branch predictor.
 */

#include "lp_perceptron.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <deque>
#include <map>

#include "champsim.h"
#include "msl/fwcounter.h"
#include <fmt/core.h>

namespace
{
template <std::size_t HISTLEN, std::size_t BITS>
class perceptron
{
  champsim::msl::sfwcounter<BITS> bias{0};
  std::array<champsim::msl::sfwcounter<BITS>, HISTLEN> weights = {};

public:
  auto _predict(std::bitset<HISTLEN> history)
  {
    auto output = bias.value();

    // find the (rest of the) dot product of the history register and the perceptron weights.
    for (std::size_t i = 0; i < std::size(history); i++) {
      if (history[i])
        output += weights[i].value();
      else
        output -= weights[i].value();
    }

    return output;
  }

  void _update(bool result, std::bitset<HISTLEN> history)
  {
    // if the branch was taken, increment the bias weight, else decrement it, with saturating arithmetic
    bias += result ? 1 : -1;

    // for each weight and corresponding bit in the history register...
    auto upd_mask = result ? history : ~history; // if the i'th bit in the history positively
                                                 // correlates with this branch outcome,
    for (std::size_t i = 0; i < std::size(upd_mask); i++) {
      // increment the corresponding weight, else decrement it, with saturating arithmetic
      weights[i] += upd_mask[i] ? 1 : -1;
    }
  }
};

constexpr std::size_t PERCEPTRON_HISTORY = 24; // history length for the global history shift register
constexpr std::size_t PERCEPTRON_BITS = 8;     // number of bits per weight
constexpr std::size_t NUM_PERCEPTRONS = 163;

constexpr std::size_t NUM_UPDATE_ENTRIES = 100; // size of buffer for keeping 'perceptron_state' for update

/* 'perceptron_state' - stores the branch prediction and keeps information
 * such as output and history needed for updating the perceptron predictor
 */
struct perceptron_state {
  uint64_t ip = 0;
  bool prediction = false;                     // prediction: 1 for taken, 0 for not taken
  long long int output = 0;                    // perceptron output
  std::bitset<PERCEPTRON_HISTORY> history = 0; // value of the history register yielding this prediction
};

std::array<perceptron<PERCEPTRON_HISTORY, PERCEPTRON_BITS>, NUM_PERCEPTRONS> perceptrons; // table of perceptrons
std::deque<perceptron_state> perceptron_state_buf;                                        // state for updating perceptron predictor
std::bitset<PERCEPTRON_HISTORY> spec_global_history;                                      // speculative global history - updated by predictor
std::bitset<PERCEPTRON_HISTORY> global_history;                                           // real global history - updated when the predictor is
                                                                                          // updated
} // namespace

PerceptronPredictor::PerceptronPredictor(LoadPredictorStats& s_lp) : LoadPredictor(s_lp)
{
  setEnabled(true);
  fmt::print("[PerceptronPredictor] {}: Initializing perceptron predictor with {} perceptrons, history length {}, and {} bits per weight.\n", __func__,
             NUM_PERCEPTRONS, PERCEPTRON_HISTORY, PERCEPTRON_BITS);
}

void PerceptronPredictor::update(Addr ip, Addr addr, bool actual_was_hit, bool predicted_was_hit)
{
  if (!isEnabled()) {
    return; // Cannot update if disabled
  }

  lp_stats.record_outcome(actual_was_hit, predicted_was_hit);

  auto state = std::find_if(std::begin(::perceptron_state_buf), std::end(::perceptron_state_buf), [ip](auto x) { return x.ip == ip; });
  if (state == std::end(::perceptron_state_buf))
    return; // Skip update because state was lost

  auto [_ip, prediction, output, history] = *state;
  ::perceptron_state_buf.erase(state);

  auto index = ip % ::NUM_PERCEPTRONS;

  // update the real global history shift register
  ::global_history <<= 1;
  ::global_history.set(0, actual_was_hit);

  // if this branch was mispredicted, restore the speculative history to the
  // last known real history
  if (prediction != actual_was_hit)
    ::spec_global_history = ::global_history;

  // if the output of the perceptron predictor is outside of the range
  // [-THETA,THETA] *and* the prediction was correct, then we don't need to
  // adjust the weights
  const int THETA = std::floor(1.93 * PERCEPTRON_HISTORY + 14); // threshold for training
  if ((output <= THETA && output >= -THETA) || (prediction != actual_was_hit))
    ::perceptrons[index]._update(actual_was_hit, history);
}

bool PerceptronPredictor::predict(Addr ip, Addr addr)
{
  if (!isEnabled()) {
    return false; // Cannot predict if disabled
  }

  lp_stats.record_prediction(true); // Record that a prediction was made

  // hash the address to get an index into the table of perceptrons
  auto index = ip % ::NUM_PERCEPTRONS;
  auto output = ::perceptrons[index]._predict(::spec_global_history);

  bool prediction = (output >= 0);

  // record the various values needed to update the predictor
  ::perceptron_state_buf.push_back({ip, prediction, output, ::spec_global_history});
  if (std::size(::perceptron_state_buf) > ::NUM_UPDATE_ENTRIES)
    ::perceptron_state_buf.pop_front();

  // update the speculative global history register
  ::spec_global_history <<= 1;
  ::spec_global_history.set(0, prediction);

  if constexpr (champsim::debug_print) {
    fmt::print("[PerceptronPredictor] {}: IP 0x{:x} -> Predicted: {}\n", __func__, ip, prediction ? "HIT" : "MISS");
  }

  return prediction;
}
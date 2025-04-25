/*
 * This file implements a basic Branch Target Buffer (BTB) structure, a Return Address Stack (RAS), and an indirect target branch prediction.
 */

#include <iostream>

#include "../common/basic_btb.h"
#include "../common/ittage.h"
#include "../common/ras.h"
#include "ooo_cpu.h"

using champsim::branch_info;

BasicBTB<1024, 8> btb[NUM_CPUS];
// BasicBTB<256, 4> l1_btb[NUM_CPUS];
// BasicBTB<4096, 8> l2_btb[NUM_CPUS];
ITTAGE* ittage[NUM_CPUS];
RAS<64, 1024> ras[NUM_CPUS];

void O3_CPU::initialize_btb()
{
  std::cout << "BTB:" << std::endl;
  btb[cpu].initialize();
  std::cout << "Indirect:" << std::endl;
  ittage[cpu] = new ITTAGE();
  std::cout << "RAS:" << std::endl;
  ras[cpu].initialize();
}

std::pair<uint64_t, uint8_t> O3_CPU::btb_prediction(uint64_t ip)
{
  auto btb_pred = btb[cpu].predict(ip);
  if (btb_pred.first == 0) {
    // no prediction for this IP
    return std::make_pair(0, false);
  }

  if (btb_pred.second == champsim::branch_info::INDIRECT) {
    return std::make_pair(ittage[cpu]->predict_brindirect(ip), true);
  } else if (btb_pred.second == champsim::branch_info::RETURN) {
    return std::make_pair(ras[cpu].predict(), true);
  } else {
    return std::make_pair(btb_pred.first, btb_pred.second != champsim::branch_info::CONDITIONAL);
  }
}

void O3_CPU::update_btb(uint64_t ip, uint64_t branch_target, uint8_t taken, uint8_t branch_type)
{
  ittage[cpu]->update_brindirect(ip, branch_type, taken, branch_target);
  ittage[cpu]->fetch_history_update(ip, branch_type, taken, branch_target);
  ras[cpu].update(ip, branch_target, taken, branch_type);
  btb[cpu].update(ip, branch_target, taken, branch_type);
}

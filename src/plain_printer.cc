/*
 *    Copyright 2023 The ChampSim Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <numeric>
#include <sstream>
#include <utility>
#include <vector>

#include "stats_printer.h"
#include <fmt/core.h>
#include <fmt/ostream.h>

void champsim::plain_printer::print(O3_CPU::stats_type stats)
{
  constexpr std::array<std::pair<std::string_view, std::size_t>, 6> types{
      {std::pair{"BRANCH_DIRECT_JUMP", BRANCH_DIRECT_JUMP}, std::pair{"BRANCH_INDIRECT", BRANCH_INDIRECT}, std::pair{"BRANCH_CONDITIONAL", BRANCH_CONDITIONAL},
       std::pair{"BRANCH_DIRECT_CALL", BRANCH_DIRECT_CALL}, std::pair{"BRANCH_INDIRECT_CALL", BRANCH_INDIRECT_CALL},
       std::pair{"BRANCH_RETURN", BRANCH_RETURN}}};

  auto total_branch = std::ceil(
      std::accumulate(std::begin(types), std::end(types), 0ll, [tbt = stats.total_branch_types](auto acc, auto next) { return acc + tbt[next.second]; }));
  auto total_mispredictions = std::ceil(
      std::accumulate(std::begin(types), std::end(types), 0ll, [btm = stats.branch_type_misses](auto acc, auto next) { return acc + btm[next.second]; }));

  fmt::print(stream, "\n{} cumulative IPC: {:.4g} instructions: {} cycles: {} wp_cycles: {}", stats.name, std::ceil(stats.instrs()) / std::ceil(stats.cycles()),
             stats.instrs(), stats.cycles(), stats.wp_cycles);
  fmt::print(stream, "\n{} wrong_path_insts: {} wrong_path_insts_skipped: {} wrong_path_insts_executed: {}", stats.name, stats.wrong_path_insts,
             stats.wrong_path_skipped, stats.wrong_path_insts_executed);
  fmt::print(stream, "\n{} instr_foot_print: {} data_foot_print: {} data_addr_foot_print: {}", stats.name, stats.instr_foot_print.size(), stats.data_foot_print.size(), stats.data_addr_foot_print.size());
  fmt::print(stream, "\n{} is_prefetch_insts: {} is_prefetch_skipped: {}", stats.name, stats.is_prefetch_insts, stats.is_prefetch_skipped);
  fmt::print(stream, "\n{} Branch Prediction Accuracy: {:.4g}% MPKI: {:.4g} Average ROB Occupancy at Mispredict: {:.4g}\n", stats.name,
             (100.0 * std::ceil(total_branch - total_mispredictions)) / total_branch, (1000.0 * total_mispredictions) / std::ceil(stats.instrs()),
             std::ceil(stats.total_rob_occupancy_at_branch_mispredict) / total_mispredictions);

  std::vector<double> mpkis;
  std::transform(std::begin(stats.branch_type_misses), std::end(stats.branch_type_misses), std::back_inserter(mpkis),
                 [instrs = stats.instrs()](auto x) { return 1000.0 * std::ceil(x) / std::ceil(instrs); });

  fmt::print(stream, "\n");
  fmt::print(stream, "Branch type MPKI\n");
  for (auto [str, idx] : types)
    fmt::print(stream, "{}: {:.3}\n", str, mpkis[idx]);
  fmt::print(stream, "\n");

  fmt::print(stream, "Instruction type breakdown\n");
  fmt::print(stream, "direct_jumps: {} ({:.3g}%)\n", stats.direct_jumps, ((double)stats.direct_jumps / (double)stats.total_instructions) * 100.0);
  fmt::print(stream, "indirect_branches: {} ({:.3g}%)\n", stats.indirect_branches,
             ((double)stats.indirect_branches / (double)stats.total_instructions) * 100.0);
  fmt::print(stream, "conditional_branches: {} ({:.3g}%)\n", stats.conditional_branches,
             ((double)stats.conditional_branches / (double)stats.total_instructions) * 100.0);
  fmt::print(stream, "direct_calls: {} ({:.3g}%)\n", stats.direct_calls, ((double)stats.direct_calls / (double)stats.total_instructions) * 100.0);
  fmt::print(stream, "indirect_calls: {} ({:.3g}%)\n", stats.indirect_calls, ((double)stats.indirect_calls / (double)stats.total_instructions) * 100.0);
  fmt::print(stream, "returns: {} ({:.3g}%)\n", stats.returns, ((double)stats.returns / (double)stats.total_instructions) * 100.0);
  fmt::print(stream, "other_branches: {} ({:.3g}%)\n", stats.other_branches, ((double)stats.other_branches / (double)stats.total_instructions) * 100.0);
  fmt::print(stream, "loads: {} ({:.3g}%)\n", stats.loads, ((double)stats.loads / (double)stats.total_instructions) * 100.0);
  fmt::print(stream, "stores: {} ({:.3g}%)\n", stats.stores, ((double)stats.stores / (double)stats.total_instructions) * 100.0);
  fmt::print(stream, "arithmetic: {} ({:.3g}%)\n", stats.arithmetic, ((double)stats.arithmetic / (double)stats.total_instructions) * 100.0);
  fmt::print(stream, "total_instructions: {}\n\n", stats.total_instructions);

  fmt::print(stream, "Fetch Idle Cycles {} ({:.3g}%)\n", stats.fetch_idle_cycles, (stats.fetch_idle_cycles / std::ceil(stats.cycles())) * 100.0);
  fmt::print(stream, "Decode Idle Cycles {} ({:.3g}%)\n", stats.decode_idle_cycles, (stats.decode_idle_cycles / std::ceil(stats.cycles())) * 100.0);
  fmt::print(stream, "Dispatch Idle Cycles {} ({:.3g}%)\n", stats.dispatch_idle_cycles, (stats.dispatch_idle_cycles / std::ceil(stats.cycles())) * 100.0);
  fmt::print(stream, "Schedule Idle Cycles {} ({:.3g}%)\n", stats.schedule_idle_cycles, (stats.schedule_idle_cycles / std::ceil(stats.cycles())) * 100.0);
  fmt::print(stream, "Execute Idle Cycles {} ({:.3g}%)\n", stats.execute_idle_cycles, (stats.execute_idle_cycles / std::ceil(stats.cycles())) * 100.0);
  fmt::print(stream, "Retire Idle Cycles {} ({:.3g}%)\n\n", stats.retire_idle_cycles, (stats.retire_idle_cycles / std::ceil(stats.cycles())) * 100.0);

  fmt::print(stream, "Fetch Starve Cycles {} ({:.3g}%)\n", stats.fetch_starve_cycles, (stats.fetch_starve_cycles / std::ceil(stats.cycles())) * 100.0);
  fmt::print(stream, "Decode Starve Cycles {} ({:.3g}%)\n", stats.decode_starve_cycles, (stats.decode_starve_cycles / std::ceil(stats.cycles())) * 100.0);
  fmt::print(stream, "Dispatch Starve Cycles {} ({:.3g}%)\n", stats.dispatch_starve_cycles, (stats.dispatch_starve_cycles / std::ceil(stats.cycles())) * 100.0);
  fmt::print(stream, "Schedule Starve Cycles {} ({:.3g}%)\n", stats.schedule_starve_cycles, (stats.schedule_starve_cycles / std::ceil(stats.cycles())) * 100.0);
  fmt::print(stream, "Execute Starve Cycles {} ({:.3g}%)\n", stats.execute_starve_cycles, (stats.execute_starve_cycles / std::ceil(stats.cycles())) * 100.0);
  fmt::print(stream, "Retire Starve Cycles {} ({:.3g}%)\n\n", stats.retire_starve_cycles, (stats.retire_starve_cycles / std::ceil(stats.cycles())) * 100.0);

  fmt::print(stream, "Total Fetch Instructions {}\n", stats.total_fetch_instructions);
  fmt::print(stream, "Total Decode Instructions {}\n", stats.total_decode_instructions);
  fmt::print(stream, "Total Dispatch Instructions {}\n", stats.total_dispatch_instructions);
  fmt::print(stream, "Total Schedule Instructions {}\n", stats.total_schedule_instructions);
  fmt::print(stream, "Total Execute Instructions {}\n", stats.total_execute_instructions);
  fmt::print(stream, "Total Retire Instructions {}\n\n", stats.total_retire_instructions);

  fmt::print(stream, "New Execution Stats\n");
  fmt::print(stream, "Execute Only WP Cycles {}\n", stats.exe_stats_new.only_wp_executed);
  fmt::print(stream, "Execute Only CP Cycles {}\n", stats.exe_stats_new.only_cp_executed);
  fmt::print(stream, "Execute CP WP Cycles {}\n", stats.exe_stats_new.cp_wp_executed);
  fmt::print(stream, "Execute nothing ROB Empty Cycles {}\n", stats.exe_stats_new.rob_empty);
  fmt::print(stream, "Execute nothing ROB Empty Repair Cycles {}\n", stats.exe_stats_new.rob_empty_repair);
  fmt::print(stream, "Execute nothing ROB Empty Fetch Stalled No WP Cycles {}\n", stats.exe_stats_new.rob_empty_fetch_stalled_no_wp);
  fmt::print(stream, "Execute nothing ROB Not Ready Not Full New Added Cycles {} \n", stats.exe_stats_new.rob_not_ready_not_full_new_added);
  fmt::print(stream, "Execute nothing ROB Not Ready Not Full No New Added Cycles {} \n", stats.exe_stats_new.rob_not_ready_not_full_no_new_added);
  fmt::print(stream, "Execute nothing ROB Not Ready Full New Added Cycles {}\n", stats.exe_stats_new.rob_not_ready_full_new_added);
  fmt::print(stream, "Execute nothing ROB Not Ready Full No New Added Cycles {}\n", stats.exe_stats_new.rob_not_ready_full_no_new_added);
  fmt::print(stream, "Execute nothing Other IDK Cycles {}\n", stats.exe_stats_new.other_idk);
  fmt::print(stream, "Execute Total Cycles Cycles {}\n\n",
             stats.exe_stats_new.only_wp_executed + stats.exe_stats_new.only_cp_executed + stats.exe_stats_new.cp_wp_executed + stats.exe_stats_new.rob_empty
                 + stats.exe_stats_new.rob_empty_repair + stats.exe_stats_new.rob_empty_fetch_stalled_no_wp + stats.exe_stats_new.rob_not_ready_not_full_new_added
                 + stats.exe_stats_new.rob_not_ready_not_full_no_new_added + stats.exe_stats_new.rob_not_ready_full_new_added
                 + stats.exe_stats_new.rob_not_ready_full_no_new_added + stats.exe_stats_new.other_idk);

  fmt::print(stream, "Resteer Events {}\n", stats.resteer_events);
  fmt::print(stream, "Resteer Penalty {:.3g}%\n", (((double)stats.resteer_events * 12) / std::ceil(stats.cycles())) * 100.0);
  fmt::print(stream, "WP Not Available Count {} Cycles {} ({:.3g}%)\n\n", stats.lack_of_WP_inst_count, stats.lack_of_WP_inst_cycles,
             ((double)stats.lack_of_WP_inst_cycles / std::ceil(stats.cycles())) * 100.0);

  fmt::print(stream, "Wrong Path Stats\n");
  fmt::print(stream, "Loads: Count {} Issued {}\n", stats.wrong_path_loads, stats.wrong_path_loads_executed);
  fmt::print(stream, "\n");

  fmt::print(stream, "Fetch Blocked Cycles {}\n", stats.fetch_blocked_cycles);
  fmt::print(stream, "IFetch Failed Events {}\n", stats.fetch_failed_events);
  fmt::print(stream, "Fetch Buffer Not Empty {}\n", stats.fetch_buffer_not_empty);
  fmt::print(stream, "Execute None Cycles {}\n", stats.execute_none_cycles);
  fmt::print(stream, "Execute Head Not Ready Cycles {}\n", stats.execute_head_not_ready);
  fmt::print(stream, "Execute Head Not Completed Cycles {}\n", stats.execute_head_not_completed);
  fmt::print(stream, "Execute Pending Cycles {}\n", stats.execute_pending_cycles);
  fmt::print(stream, "Execute Load Blocked Cycles {}\n", stats.execute_load_blocked_cycles);
  fmt::print(stream, "Scheduler None Cycles {}\n", stats.sched_none_cycles);
  fmt::print(stream, "LQ Full Events {}\n", stats.lq_full_events);
  fmt::print(stream, "SQ Full Events {}\n", stats.sq_full_events);
  fmt::print(stream, "Non Branch Squashes {}\n", stats.non_branch_squashes);
  fmt::print(stream, "ROB Full Cycles {}\n", stats.rob_full_cycles);
  fmt::print(stream, "ROB Empty Cycles {}\n", stats.rob_empty_cycles);
  fmt::print(stream, "ROB Full Events {}\n", stats.rob_full_events);
  fmt::print(stream, "ROB Empty Events {}\n", stats.rob_empty_events);

  fmt::print(stream, "\n");

  fmt::print(stream, "Inst Stats\n");
  fmt::print(stream, "Loads Success: {} \n", stats.loads_success);
  fmt::print(stream, "Loads Executed: {} \n", stats.loads_executed);
  fmt::print(stream, "Loads Retired: {} \n", stats.loads_retired);
  fmt::print(stream, "\n");
}

void champsim::plain_printer::print(CACHE::stats_type stats)
{
  constexpr std::array<std::pair<std::string_view, std::size_t>, 5> types{
      {std::pair{"LOAD", champsim::to_underlying(access_type::LOAD)}, std::pair{"RFO", champsim::to_underlying(access_type::RFO)},
       std::pair{"PREFETCH", champsim::to_underlying(access_type::PREFETCH)}, std::pair{"WRITE", champsim::to_underlying(access_type::WRITE)},
       std::pair{"TRANSLATION", champsim::to_underlying(access_type::TRANSLATION)}}};

  for (std::size_t cpu = 0; cpu < NUM_CPUS; ++cpu) {
    uint64_t TOTAL_HIT = 0, TOTAL_MISS = 0;
    for (const auto& type : types) {
      TOTAL_HIT += stats.hits.at(type.second).at(cpu);
      TOTAL_MISS += stats.misses.at(type.second).at(cpu);
    }

    fmt::print(stream, "{} TOTAL        ACCESS: {:10d} HIT: {:10d} MISS: {:10d}\n", stats.name, TOTAL_HIT + TOTAL_MISS, TOTAL_HIT, TOTAL_MISS);
    for (const auto& type : types) {
      fmt::print(stream, "{} {:<12s} ACCESS: {:10d} HIT: {:10d} MISS: {:10d}\n", stats.name, type.first,
                 stats.hits[type.second][cpu] + stats.misses[type.second][cpu], stats.hits[type.second][cpu], stats.misses[type.second][cpu]);
    }

    fmt::print(stream, "{} PREFETCH REQUESTED: {:10} ISSUED: {:10} USEFUL: {:10} USELESS: {:10}\n", stats.name, stats.pf_requested, stats.pf_issued,
               stats.pf_useful, stats.pf_useless);
    fmt::print(stream, "{} WRONG-PATH ACCESS: {:10} LOAD: {:10} USEFULL: {:10} FILL: {:10} USELESS: {:10}\n", stats.name, stats.wp_load + stats.wp_store,
               stats.wp_load, stats.wp_useful, stats.wp_fill, stats.wp_useless);
    fmt::print(stream, "{} POLLUTION: {:.4g} SAMPLES: {:10} WP_FILL: {:10} WP_MISS: {:10} CP_FILL: {:10} CP_MISS: {:10}\n", stats.name, stats.avg_pollution, stats.pollution_samples, stats.wp_fill,
               stats.wp_miss, stats.cp_fill, stats.cp_miss);
    fmt::print(stream, "{} INSTR REQ: {}  HIT: {}  MISS: {} WP_REQ: {} WP_HIT: {} WP_MISS: {} \n", stats.name, stats.instr_req, stats.istr_hit, stats.istr_miss,
               stats.wp_instr_req, stats.wp_istr_hit, stats.wp_istr_miss);
    fmt::print(stream, "{} DATA REQ: {}  HIT: {}  MISS: {} WP_REQ: {} WP_HIT: {} WP_MISS: {} \n", stats.name, stats.data_req, stats.data_hit, stats.data_miss,
               stats.wp_data_req, stats.wp_data_hit, stats.wp_data_miss);
    fmt::print(stream, "{} AVERAGE MISS LATENCY: {:.4g} cycles\n", stats.name, stats.avg_miss_latency);
    fmt::print(stream, "{} AVERAGE WP MISS LATENCY: {:.4g} cycles\n", stats.name, stats.avg_wp_miss_latency);
    fmt::print(stream, "{} AVERAGE CP MISS LATENCY: {:.4g} cycles\n", stats.name, stats.avg_cp_miss_latency);
    fmt::print(stream, "{} AVERAGE INSTR MISS LATENCY: {:.4g} cycles\n", stats.name, stats.avg_instr_miss_latency);
    fmt::print(stream, "{} AVERAGE WP INSTR MISS LATENCY: {:.4g} cycles\n", stats.name, stats.avg_wp_instr_miss_latency);
    fmt::print(stream, "{} AVERAGE CP INSTR MISS LATENCY: {:.4g} cycles\n", stats.name, stats.avg_cp_instr_miss_latency);
    fmt::print(stream, "{} AVERAGE DATA MISS LATENCY: {:.4g} cycles\n", stats.name, stats.avg_data_miss_latency);
    fmt::print(stream, "{} AVERAGE WP DATA MISS LATENCY: {:.4g} cycles\n", stats.name, stats.avg_wp_data_miss_latency);
    fmt::print(stream, "{} AVERAGE CP DATA MISS LATENCY: {:.4g} cycles\n\n", stats.name, stats.avg_cp_data_miss_latency);
  }
}

void champsim::plain_printer::print(DRAM_CHANNEL::stats_type stats)
{
  fmt::print(stream, "\n{} RQ ROW_BUFFER_HIT: {:10}\n  ROW_BUFFER_MISS: {:10}\n", stats.name, stats.RQ_ROW_BUFFER_HIT, stats.RQ_ROW_BUFFER_MISS);
  if (stats.dbus_count_congested > 0)
    fmt::print(stream, " AVG DBUS CONGESTED CYCLE: {:.4g}\n", std::ceil(stats.dbus_cycle_congested) / std::ceil(stats.dbus_count_congested));
  else
    fmt::print(stream, " AVG DBUS CONGESTED CYCLE: -\n");
  fmt::print(stream, "WQ ROW_BUFFER_HIT: {:10}\n  ROW_BUFFER_MISS: {:10}\n  FULL: {:10}\n", stats.name, stats.WQ_ROW_BUFFER_HIT, stats.WQ_ROW_BUFFER_MISS,
             stats.WQ_FULL);
}

void champsim::plain_printer::print(champsim::phase_stats& stats)
{
  fmt::print(stream, "=== {} ===\n", stats.name);

  int i = 0;
  for (auto tn : stats.trace_names)
    fmt::print(stream, "CPU {} runs {}", i++, tn);

  if (NUM_CPUS > 1) {
    fmt::print(stream, "\nTotal Simulation Statistics (not including warmup)\n");

    for (const auto& stat : stats.sim_cpu_stats)
      print(stat);

    for (const auto& stat : stats.sim_cache_stats)
      print(stat);
  }

  fmt::print(stream, "\nRegion of Interest Statistics\n");

  for (const auto& stat : stats.roi_cpu_stats)
    print(stat);

  for (const auto& stat : stats.roi_cache_stats)
    print(stat);

  fmt::print(stream, "\nDRAM Statistics\n");
  for (const auto& stat : stats.roi_dram_stats)
    print(stat);
}

void champsim::plain_printer::print(std::vector<phase_stats>& stats)
{
  for (auto p : stats)
    print(p);
}

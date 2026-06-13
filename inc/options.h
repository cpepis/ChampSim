#ifndef OPTIONS_H
#define OPTIONS_H

#include <CLI/CLI.hpp>

#include "environment.h"

namespace champsim
{
class Options
{
private:
  // CPU parameters
  std::size_t ifetch_buffer_size;
  std::size_t decode_buffer_size;
  std::size_t dispatch_buffer_size;
  std::size_t rob_size;
  std::size_t register_file_size;
  std::size_t lq_size;
  std::size_t sq_size;

  long int fetch_width;
  long int decode_width;
  long int dispatch_width;
  long int execute_width;

  long int lq_width;
  long int sq_width;

  long int retire_width;

  unsigned mispredict_penalty;
  unsigned scheduler_size;
  unsigned decode_latency;
  unsigned dispatch_latency;
  unsigned schedule_latency;
  unsigned execute_latency;

  long int l1i_bandwidth;
  long int l1d_bandwidth;

  void set(environment& env);

public:
  Options() = default;
  void init(CLI::App& app);
  void update(environment& env);
};

} // namespace champsim

#endif

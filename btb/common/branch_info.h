#ifndef BRANCH_INFO_H
#define BRANCH_INFO_H

namespace champsim
{
enum class branch_info {
  INDIRECT,
  RETURN,
  ALWAYS_TAKEN,
  CONDITIONAL,
};
}
#endif // BRANCH_INFO_H

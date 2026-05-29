#ifndef TASKQUEUE_BROKER_STATS_H_
#define TASKQUEUE_BROKER_STATS_H_

#include <cstddef>

namespace tq {

struct BrokerStats {
  std::size_t pending_count = 0;
  std::size_t delayed_count = 0;
  std::size_t running_count = 0;
  std::size_t dead_count = 0;
};

}  // namespace tq

#endif  // TASKQUEUE_BROKER_STATS_H_

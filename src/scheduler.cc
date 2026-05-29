#include "taskqueue/scheduler.h"

#include "taskqueue/clock.h"

namespace tq {

void RunSchedulerTick(Broker& broker, std::int64_t visibility_timeout_ms) {
  const std::int64_t now_ms = NowUnixMs();
  broker.PromoteDueTasks(now_ms);
  broker.ReclaimStaleTasks(now_ms, visibility_timeout_ms);
}

}  // namespace tq

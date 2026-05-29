#include "taskqueue/retry_policy.h"

#include <cmath>

#include "taskqueue/task_message.h"

namespace tq {

std::int64_t ComputeRetryDelayMs(const RetryPolicy& policy, int retry_count) {
  if (retry_count <= 0) {
    return policy.base_delay_ms;
  }

  const double delay =
      static_cast<double>(policy.base_delay_ms) *
      std::pow(policy.multiplier, static_cast<double>(retry_count - 1));
  return static_cast<std::int64_t>(delay);
}

bool ShouldRetry(const TaskMessage& task) {
  return task.retry_count < task.retry_policy.max_retries;
}

}  // namespace tq

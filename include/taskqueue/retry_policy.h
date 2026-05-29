#ifndef TASKQUEUE_RETRY_POLICY_H_
#define TASKQUEUE_RETRY_POLICY_H_

#include <cstdint>

namespace tq {

struct TaskMessage;

struct RetryPolicy {
  int max_retries = 3;
  std::int64_t base_delay_ms = 1000;
  double multiplier = 2.0;

  bool operator==(const RetryPolicy& other) const {
    return max_retries == other.max_retries &&
           base_delay_ms == other.base_delay_ms &&
           multiplier == other.multiplier;
  }
};

std::int64_t ComputeRetryDelayMs(const RetryPolicy& policy, int retry_count);
bool ShouldRetry(const TaskMessage& task);

}  // namespace tq

#endif  // TASKQUEUE_RETRY_POLICY_H_

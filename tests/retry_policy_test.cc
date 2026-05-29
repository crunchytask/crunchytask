#include "taskqueue/retry_policy.h"
#include "taskqueue/task_message.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ComputeRetryDelayMs applies exponential backoff", "[retry_policy]") {
  tq::RetryPolicy policy;
  policy.base_delay_ms = 1000;
  policy.multiplier = 2.0;

  REQUIRE(tq::ComputeRetryDelayMs(policy, 1) == 1000);
  REQUIRE(tq::ComputeRetryDelayMs(policy, 2) == 2000);
  REQUIRE(tq::ComputeRetryDelayMs(policy, 3) == 4000);
}

TEST_CASE("ShouldRetry respects max_retries", "[retry_policy]") {
  tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  task.retry_policy.max_retries = 2;

  task.retry_count = 0;
  REQUIRE(tq::ShouldRetry(task));

  task.retry_count = 1;
  REQUIRE(tq::ShouldRetry(task));

  task.retry_count = 2;
  REQUIRE_FALSE(tq::ShouldRetry(task));
}

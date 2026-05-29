#include "fake_broker.h"

#include <catch2/catch_test_macros.hpp>

#include "taskqueue/clock.h"
#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"
#include "taskqueue/worker.h"

namespace {

tq::TaskHandler MakeAlwaysFailHandler() {
  return [](const nlohmann::json&) {
    return tq::TaskResult::Failure("permanent failure");
  };
}

}  // namespace

TEST_CASE("Retry uses delayed queue and increments retry count",
          "[unit][failure][retry]") {
  tq::testing::FakeBroker broker;
  tq::TaskMessage task = tq::TaskMessage::Create("flaky", nlohmann::json{});
  task.retry_policy.max_retries = 3;
  task.retry_policy.base_delay_ms = 0;
  broker.Enqueue(task);

  const auto first = broker.Reserve();
  REQUIRE(first.has_value());
  broker.Retry(*first, "attempt 1");
  REQUIRE(broker.DelayedCount() == 1);
  REQUIRE(broker.RunningCount() == 0);

  broker.PromoteDueTasks(tq::NowUnixMs());
  const auto second = broker.Reserve();
  REQUIRE(second.has_value());
  REQUIRE(second->retry_count == 1);
  REQUIRE(second->last_error == "attempt 1");
}

TEST_CASE("Exhausted retries land in dead-letter queue", "[unit][failure][dead_letter]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);
  worker.RegisterTask("always_fail", MakeAlwaysFailHandler());

  tq::TaskMessage task = tq::TaskMessage::Create("always_fail", nlohmann::json{});
  task.retry_policy.max_retries = 2;
  task.retry_policy.base_delay_ms = 0;
  broker.Enqueue(task);

  REQUIRE(worker.RunOnce());
  broker.PromoteDueTasks(tq::NowUnixMs());
  REQUIRE(worker.RunOnce());
  broker.PromoteDueTasks(tq::NowUnixMs());
  REQUIRE(worker.RunOnce());

  REQUIRE(broker.DeadCount() == 1);
  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kDead);
}

TEST_CASE("Visibility timeout requeues abandoned running task",
          "[unit][failure][crash_recovery]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);

  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  const std::int64_t timeout_ms = 50;
  REQUIRE(broker.ReclaimStaleTasks(*reserved->reserved_at_ms + timeout_ms + 1,
                                   timeout_ms) == 1);
  REQUIRE(broker.PendingCount() == 1);
  REQUIRE(broker.RunningCount() == 0);
}

#if defined(TASKQUEUE_HAS_REDIS) && TASKQUEUE_ENABLE_INTEGRATION_TESTS

#include <cstdlib>

#include <sw/redis++/redis++.h>

#include "integration_guard.h"
#include "taskqueue/clock.h"
#include "taskqueue/redis_broker.h"
#include "taskqueue/redis_keys.h"

namespace {

void ClearBrokerKeys(const std::string& redis_uri) {
  sw::redis::Redis redis(redis_uri);
  redis.del({tq::redis_keys::kPending, tq::redis_keys::kDelayed,
             tq::redis_keys::kRunning, tq::redis_keys::kStatus,
             tq::redis_keys::kResults, tq::redis_keys::kDead,
             tq::redis_keys::kFailures});
}

}  // namespace

TEST_CASE("Worker retry exhaustion reaches dead letter in Redis",
          "[integration][failure]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  tq::Worker worker(broker);
  worker.RegisterTask("always_fail", MakeAlwaysFailHandler());

  tq::TaskMessage task = tq::TaskMessage::Create("always_fail", nlohmann::json{});
  task.retry_policy.max_retries = 1;
  task.retry_policy.base_delay_ms = 0;
  broker.Enqueue(task);

  REQUIRE(worker.RunOnce());
  broker.PromoteDueTasks(tq::NowUnixMs());
  REQUIRE(worker.RunOnce());

  REQUIRE(broker.GetStats().dead_count == 1);
  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kDead);
}

#endif

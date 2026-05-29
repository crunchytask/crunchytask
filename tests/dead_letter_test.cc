#include "fake_broker.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "taskqueue/broker.h"
#include "taskqueue/clock.h"
#include "taskqueue/task_message.h"
#include "taskqueue/worker.h"

namespace {

tq::TaskHandler MakeAlwaysFailHandler() {
  return [](const nlohmann::json&) {
    return tq::TaskResult::Failure("permanent");
  };
}

}  // namespace

TEST_CASE("FakeBroker stores failed tasks in dead-letter queue", "[dead_letter]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  broker.Fail(reserved->id, "handler crashed");
  REQUIRE(broker.DeadCount() == 1);
  REQUIRE(broker.RunningCount() == 0);

  const auto status = broker.GetStatus(reserved->id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kDead);

  const auto dead_tasks = broker.ListDeadTasks();
  REQUIRE(dead_tasks.size() == 1);
  REQUIRE(dead_tasks.front().id == reserved->id);
  REQUIRE(dead_tasks.front().last_error == "handler crashed");

  const auto reason = broker.GetFailureReason(reserved->id);
  REQUIRE(reason.Ok());
  REQUIRE(reason.Value() == "handler crashed");
}

TEST_CASE("FakeBroker retries dead-letter task", "[dead_letter]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  broker.Fail(reserved->id, "handler crashed");
  REQUIRE(broker.DeadCount() == 1);

  const auto retried = broker.RetryDeadTask(reserved->id);
  REQUIRE(retried.Ok());
  REQUIRE(broker.DeadCount() == 0);
  REQUIRE(broker.PendingCount() == 1);

  const auto status = broker.GetStatus(reserved->id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kPending);

  const auto requeued = broker.Reserve();
  REQUIRE(requeued.has_value());
  REQUIRE(requeued->retry_count == 0);
  REQUIRE(requeued->last_error.empty());
}

TEST_CASE("Worker moves exhausted tasks to dead-letter queue", "[dead_letter]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);
  worker.RegisterTask("always_fail", MakeAlwaysFailHandler());

  tq::TaskMessage task = tq::TaskMessage::Create("always_fail", nlohmann::json{});
  task.retry_policy.max_retries = 1;
  task.retry_policy.base_delay_ms = 0;
  broker.Enqueue(task);

  REQUIRE(worker.RunOnce());
  broker.PromoteDueTasks(tq::NowUnixMs());
  REQUIRE(worker.RunOnce());

  REQUIRE(broker.DeadCount() == 1);
  const auto dead_tasks = broker.ListDeadTasks();
  REQUIRE(dead_tasks.size() == 1);
  REQUIRE(dead_tasks.front().id == task.id);

  const auto reason = broker.GetFailureReason(task.id);
  REQUIRE(reason.Ok());
  REQUIRE(reason.Value() == "permanent");
}

#if defined(TASKQUEUE_HAS_REDIS) && TASKQUEUE_ENABLE_INTEGRATION_TESTS

#include <cstdlib>

#include <sw/redis++/redis++.h>

#include "integration_guard.h"
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

TEST_CASE("RedisBroker lists and retries dead-letter tasks",
          "[integration][dead_letter]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());

  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  broker.Fail(reserved->id, "handler crashed");

  sw::redis::Redis redis(tq::testing::RedisUri());
  REQUIRE(redis.llen(tq::redis_keys::kDead) == 1);

  const auto dead_tasks = broker.ListDeadTasks();
  REQUIRE(dead_tasks.size() == 1);
  REQUIRE(dead_tasks.front().id == reserved->id);

  const auto reason = broker.GetFailureReason(reserved->id);
  REQUIRE(reason.Ok());
  REQUIRE(reason.Value() == "handler crashed");

  const auto retried = broker.RetryDeadTask(reserved->id);
  REQUIRE(retried.Ok());
  REQUIRE(redis.llen(tq::redis_keys::kDead) == 0);
  REQUIRE(redis.llen(tq::redis_keys::kPending) == 1);

  const auto status = broker.GetStatus(reserved->id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kPending);
}

#endif

#include "fake_broker.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "taskqueue/broker.h"
#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"
#include "taskqueue/worker.h"

namespace {

tq::TaskHandler MakeAddHandler() {
  return [](const nlohmann::json& payload) {
    const int a = payload.at("a").get<int>();
    const int b = payload.at("b").get<int>();
    return tq::TaskResult::Success({{"result", a + b}});
  };
}

}  // namespace

TEST_CASE("FakeBroker does not reclaim running task before visibility timeout",
          "[crash_recovery]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);

  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  REQUIRE(reserved->reserved_at_ms.has_value());
  REQUIRE(broker.RunningCount() == 1);

  const std::int64_t timeout_ms = 1000;
  const std::int64_t before_deadline = *reserved->reserved_at_ms + timeout_ms;
  REQUIRE(broker.ReclaimStaleTasks(before_deadline, timeout_ms) == 0);
  REQUIRE(broker.RunningCount() == 1);
  REQUIRE(broker.PendingCount() == 0);
}

TEST_CASE("FakeBroker reclaims stale running task to pending queue",
          "[crash_recovery]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);

  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  REQUIRE(reserved->reserved_at_ms.has_value());

  const std::int64_t timeout_ms = 1000;
  const std::int64_t after_deadline = *reserved->reserved_at_ms + timeout_ms + 1;
  REQUIRE(broker.ReclaimStaleTasks(after_deadline, timeout_ms) == 1);
  REQUIRE(broker.RunningCount() == 0);
  REQUIRE(broker.PendingCount() == 1);

  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kPending);

  const auto rerun = broker.Reserve();
  REQUIRE(rerun.has_value());
  REQUIRE(rerun->id == task.id);
  REQUIRE(rerun->reserved_at_ms.has_value());
}

TEST_CASE("Simulated worker crash allows another worker to finish task",
          "[crash_recovery]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task =
      tq::TaskMessage::Create("add", nlohmann::json{{"a", 2}, {"b", 3}});
  broker.Enqueue(task);

  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  REQUIRE(reserved->reserved_at_ms.has_value());

  const std::int64_t timeout_ms = 50;
  broker.ReclaimStaleTasks(*reserved->reserved_at_ms + timeout_ms + 1,
                           timeout_ms);

  tq::Worker worker(broker);
  worker.RegisterTask("add", MakeAddHandler());
  worker.SetVisibilityTimeout(std::chrono::milliseconds(timeout_ms));
  REQUIRE(worker.RunOnce());

  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kSucceeded);
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

TEST_CASE("RedisBroker reclaims stale running task", "[integration][crash_recovery]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());

  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  REQUIRE(reserved->reserved_at_ms.has_value());

  sw::redis::Redis redis(tq::testing::RedisUri());
  REQUIRE(redis.hexists(tq::redis_keys::kRunning, task.id.Value()));

  const std::int64_t timeout_ms = 1000;
  const std::int64_t reclaim_at = *reserved->reserved_at_ms + timeout_ms + 1;
  REQUIRE(broker.ReclaimStaleTasks(reclaim_at, timeout_ms) == 1);
  REQUIRE_FALSE(redis.hexists(tq::redis_keys::kRunning, task.id.Value()));
  REQUIRE(redis.llen(tq::redis_keys::kPending) == 1);

  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kPending);
}

#endif

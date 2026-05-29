#include "fake_broker.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "taskqueue/broker.h"
#include "taskqueue/clock.h"
#include "taskqueue/scheduler.h"
#include "taskqueue/task_message.h"
#include "taskqueue/worker.h"

TEST_CASE("FakeBroker enqueues delayed tasks into delayed queue", "[broker][delayed]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task =
      tq::TaskMessage::CreateWithDelay("add", nlohmann::json{{"a", 1}}, 5000);

  broker.Enqueue(task);
  REQUIRE(broker.PendingCount() == 0);
  REQUIRE(broker.DelayedCount() == 1);
  REQUIRE_FALSE(broker.Reserve().has_value());

  broker.PromoteDueTasks(*task.run_at_ms - 1);
  REQUIRE(broker.PendingCount() == 0);
  REQUIRE(broker.DelayedCount() == 1);

  tq::RunSchedulerTick(broker, tq::Worker::kDefaultVisibilityTimeoutMs);
  if (*task.run_at_ms > tq::NowUnixMs()) {
    REQUIRE(broker.PendingCount() == 0);
    broker.PromoteDueTasks(*task.run_at_ms);
  }

  REQUIRE(broker.PendingCount() == 1);
  REQUIRE(broker.DelayedCount() == 0);

  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  REQUIRE(reserved->id == task.id);
  REQUIRE(reserved->status == tq::TaskStatus::kRunning);
  REQUIRE_FALSE(reserved->run_at_ms.has_value());
}

TEST_CASE("FakeBroker enqueues past run_at immediately", "[broker][delayed]") {
  tq::testing::FakeBroker broker;
  tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  task.run_at_ms = tq::NowUnixMs() - 1;

  broker.Enqueue(task);
  REQUIRE(broker.PendingCount() == 1);
  REQUIRE(broker.DelayedCount() == 0);
  REQUIRE(broker.Reserve().has_value());
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

TEST_CASE("RedisBroker enqueues delayed tasks", "[integration][delayed]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());

  const tq::TaskMessage task =
      tq::TaskMessage::CreateWithDelay("add", nlohmann::json{}, 5000);
  broker.Enqueue(task);

  sw::redis::Redis redis(tq::testing::RedisUri());
  REQUIRE(redis.llen(tq::redis_keys::kPending) == 0);
  REQUIRE(redis.zcard(tq::redis_keys::kDelayed) == 1);
  REQUIRE_FALSE(broker.Reserve().has_value());

  broker.PromoteDueTasks(*task.run_at_ms);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  REQUIRE(reserved->id == task.id);
}

#endif

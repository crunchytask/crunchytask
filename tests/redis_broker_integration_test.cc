#if defined(TASKQUEUE_HAS_REDIS) && TASKQUEUE_ENABLE_INTEGRATION_TESTS

#include <cstdlib>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <sw/redis++/redis++.h>

#include "integration_guard.h"
#include "taskqueue/redis_broker.h"
#include "taskqueue/redis_keys.h"
#include "taskqueue/task_message.h"
#include "taskqueue/clock.h"

namespace {

void ClearBrokerKeys(const std::string& redis_uri) {
  sw::redis::Redis redis(redis_uri);
  redis.del({tq::redis_keys::kPending, tq::redis_keys::kDelayed,
             tq::redis_keys::kRunning, tq::redis_keys::kStatus,
             tq::redis_keys::kResults, tq::redis_keys::kDead,
             tq::redis_keys::kFailures});
}

}  // namespace

TEST_CASE("RedisBroker enqueue reserve ack flow", "[integration][redis]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());

  const tq::TaskMessage task =
      tq::TaskMessage::Create("add", nlohmann::json{{"a", 2}, {"b", 3}});
  const tq::TaskId id = broker.Enqueue(task);
  REQUIRE(id == task.id);

  const auto pending_status = broker.GetStatus(id);
  REQUIRE(pending_status.Ok());
  REQUIRE(pending_status.Value() == tq::TaskStatus::kPending);

  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  REQUIRE(reserved->id == id);
  REQUIRE(reserved->status == tq::TaskStatus::kRunning);

  const auto running_status = broker.GetStatus(id);
  REQUIRE(running_status.Ok());
  REQUIRE(running_status.Value() == tq::TaskStatus::kRunning);

  broker.Ack(id);

  const auto succeeded_status = broker.GetStatus(id);
  REQUIRE(succeeded_status.Ok());
  REQUIRE(succeeded_status.Value() == tq::TaskStatus::kSucceeded);

  sw::redis::Redis redis(tq::testing::RedisUri());
  REQUIRE(redis.hexists(tq::redis_keys::kResults, id.Value()));
  REQUIRE_FALSE(redis.hexists(tq::redis_keys::kRunning, id.Value()));
}

TEST_CASE("RedisBroker stores failure reason", "[integration][redis]") {
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

  const auto status = broker.GetStatus(reserved->id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kDead);

  sw::redis::Redis redis(tq::testing::RedisUri());
  const auto reason = redis.hget(tq::redis_keys::kFailures, reserved->id.Value());
  REQUIRE(reason.has_value());
  REQUIRE(*reason == "handler crashed");
}

TEST_CASE("RedisBroker retries with delayed queue", "[integration][redis]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());

  tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  task.retry_policy.base_delay_ms = 0;
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  broker.Retry(*reserved, "transient");
  REQUIRE(broker.GetStatus(reserved->id).Value() == tq::TaskStatus::kRetrying);

  sw::redis::Redis redis(tq::testing::RedisUri());
  REQUIRE(redis.zcard(tq::redis_keys::kDelayed) == 1);
  REQUIRE_FALSE(redis.hexists(tq::redis_keys::kRunning, reserved->id.Value()));

  const auto reason = redis.hget(tq::redis_keys::kFailures, reserved->id.Value());
  REQUIRE(reason.has_value());
  REQUIRE(*reason == "transient");

  broker.PromoteDueTasks(tq::NowUnixMs());
  const auto retried = broker.Reserve();
  REQUIRE(retried.has_value());
  REQUIRE(retried->retry_count == 1);
  REQUIRE(retried->last_error == "transient");
}

#endif  // TASKQUEUE_HAS_REDIS && TASKQUEUE_ENABLE_INTEGRATION_TESTS

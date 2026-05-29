#include "fake_broker.h"

#include <catch2/catch_test_macros.hpp>

#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"

TEST_CASE("FakeBroker reports queue stats", "[broker][stats]") {
  tq::testing::FakeBroker broker;
  tq::TaskMessage pending = tq::TaskMessage::Create("add", nlohmann::json{});
  tq::TaskMessage delayed =
      tq::TaskMessage::CreateWithDelay("slow", nlohmann::json{}, 5000);
  broker.Enqueue(pending);
  broker.Enqueue(delayed);

  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  broker.Fail(reserved->id, "boom");

  const tq::BrokerStats stats = broker.GetStats();
  REQUIRE(stats.pending_count == 0);
  REQUIRE(stats.delayed_count == 1);
  REQUIRE(stats.running_count == 0);
  REQUIRE(stats.dead_count == 1);
}

TEST_CASE("FakeBroker stores task results on ack", "[broker][stats]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  broker.Ack(reserved->id);

  const auto result = broker.GetTaskResult(reserved->id);
  REQUIRE(result.Ok());
  REQUIRE(result.Value().success);
}

#if defined(TASKQUEUE_HAS_REDIS) && TASKQUEUE_ENABLE_INTEGRATION_TESTS

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

TEST_CASE("RedisBroker reports queue stats", "[integration][stats]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());

  broker.Enqueue(tq::TaskMessage::Create("add", nlohmann::json{}));
  broker.Enqueue(
      tq::TaskMessage::CreateWithDelay("slow", nlohmann::json{}, 5000));

  const tq::BrokerStats stats = broker.GetStats();
  REQUIRE(stats.pending_count == 1);
  REQUIRE(stats.delayed_count == 1);
  REQUIRE(stats.running_count == 0);
  REQUIRE(stats.dead_count == 0);
}

TEST_CASE("RedisBroker stores task results on ack", "[integration][stats]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());

  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  broker.Ack(reserved->id);

  const auto result = broker.GetTaskResult(reserved->id);
  REQUIRE(result.Ok());
  REQUIRE(result.Value().success);
}

#endif

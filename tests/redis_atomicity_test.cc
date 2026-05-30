#if defined(TASKQUEUE_HAS_REDIS) && TASKQUEUE_ENABLE_INTEGRATION_TESTS

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <sw/redis++/redis++.h>

#include "integration_guard.h"
#include "taskqueue/clock.h"
#include "taskqueue/redis_broker.h"
#include "taskqueue/redis_keys.h"
#include "taskqueue/task_json.h"
#include "taskqueue/task_message.h"

namespace {

void ClearBrokerKeys(const std::string& redis_uri) {
  sw::redis::Redis redis(redis_uri);
  redis.del({tq::redis_keys::kPending, tq::redis_keys::kDelayed,
             tq::redis_keys::kRunning, tq::redis_keys::kStatus,
             tq::redis_keys::kResults, tq::redis_keys::kDead,
             tq::redis_keys::kFailures});
}

bool PendingContainsTaskId(sw::redis::Redis& redis, const std::string& task_id) {
  std::vector<std::string> items;
  redis.lrange(tq::redis_keys::kPending, 0, -1, std::back_inserter(items));
  for (const std::string& json_text : items) {
    const auto parsed = tq::TaskMessageFromJsonString(json_text);
    if (parsed.Ok() && parsed.Value().id.Value() == task_id) {
      return true;
    }
  }
  return false;
}

bool DelayedContainsTaskId(sw::redis::Redis& redis, const std::string& task_id) {
  std::vector<std::string> items;
  redis.zrange(tq::redis_keys::kDelayed, 0, -1, std::back_inserter(items));
  for (const std::string& json_text : items) {
    const auto parsed = tq::TaskMessageFromJsonString(json_text);
    if (parsed.Ok() && parsed.Value().id.Value() == task_id) {
      return true;
    }
  }
  return false;
}

bool DeadContainsTaskId(sw::redis::Redis& redis, const std::string& task_id) {
  std::vector<std::string> items;
  redis.lrange(tq::redis_keys::kDead, 0, -1, std::back_inserter(items));
  for (const std::string& json_text : items) {
    const auto parsed = tq::TaskMessageFromJsonString(json_text);
    if (parsed.Ok() && parsed.Value().id.Value() == task_id) {
      return true;
    }
  }
  return false;
}

bool TaskIsAccountedFor(sw::redis::Redis& redis, const std::string& task_id) {
  if (redis.hexists(tq::redis_keys::kRunning, task_id)) {
    return true;
  }
  if (PendingContainsTaskId(redis, task_id)) {
    return true;
  }
  if (DelayedContainsTaskId(redis, task_id)) {
    return true;
  }
  if (DeadContainsTaskId(redis, task_id)) {
    return true;
  }
  return false;
}

}  // namespace

TEST_CASE("RedisBroker reserve keeps task accounted for", "[integration][atomicity]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  sw::redis::Redis redis(tq::testing::RedisUri());

  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  const tq::TaskId id = broker.Enqueue(task);

  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  REQUIRE(reserved->id == id);
  REQUIRE(TaskIsAccountedFor(redis, id.Value()));
  REQUIRE(redis.hexists(tq::redis_keys::kRunning, id.Value()));
  REQUIRE_FALSE(PendingContainsTaskId(redis, id.Value()));
}

TEST_CASE("RedisBroker promote keeps delayed task accounted for",
          "[integration][atomicity]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  sw::redis::Redis redis(tq::testing::RedisUri());

  const tq::TaskMessage task =
      tq::TaskMessage::CreateWithDelay("add", nlohmann::json{}, 5000);
  broker.Enqueue(task);
  REQUIRE(DelayedContainsTaskId(redis, task.id.Value()));

  broker.PromoteDueTasks(*task.run_at_ms);
  REQUIRE(TaskIsAccountedFor(redis, task.id.Value()));
  REQUIRE(PendingContainsTaskId(redis, task.id.Value()));
  REQUIRE_FALSE(DelayedContainsTaskId(redis, task.id.Value()));
}

TEST_CASE("RedisBroker reclaim keeps stale task accounted for",
          "[integration][atomicity]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  sw::redis::Redis redis(tq::testing::RedisUri());

  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  REQUIRE(reserved->reserved_at_ms.has_value());

  const std::int64_t timeout_ms = 1000;
  const std::int64_t reclaim_at = *reserved->reserved_at_ms + timeout_ms + 1;
  REQUIRE(broker.ReclaimStaleTasks(reclaim_at, timeout_ms) == 1);

  REQUIRE(TaskIsAccountedFor(redis, task.id.Value()));
  REQUIRE(PendingContainsTaskId(redis, task.id.Value()));
  REQUIRE_FALSE(redis.hexists(tq::redis_keys::kRunning, task.id.Value()));
}

TEST_CASE("RedisBroker retry keeps task accounted for", "[integration][atomicity]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  sw::redis::Redis redis(tq::testing::RedisUri());

  tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  task.retry_policy.base_delay_ms = 0;
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  broker.Retry(*reserved, "transient");
  REQUIRE(TaskIsAccountedFor(redis, task.id.Value()));
  REQUIRE(DelayedContainsTaskId(redis, task.id.Value()));
  REQUIRE_FALSE(redis.hexists(tq::redis_keys::kRunning, task.id.Value()));
}

TEST_CASE("RedisBroker fail keeps task accounted for", "[integration][atomicity]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  sw::redis::Redis redis(tq::testing::RedisUri());

  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  broker.Fail(reserved->id, "permanent");
  REQUIRE(TaskIsAccountedFor(redis, task.id.Value()));
  REQUIRE(DeadContainsTaskId(redis, task.id.Value()));
  REQUIRE_FALSE(redis.hexists(tq::redis_keys::kRunning, task.id.Value()));
}

TEST_CASE("RedisBroker concurrent promote does not leave delayed tasks stranded",
          "[integration][atomicity]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker_a(tq::testing::RedisUri());
  tq::RedisBroker broker_b(tq::testing::RedisUri());
  sw::redis::Redis redis(tq::testing::RedisUri());

  const tq::TaskMessage task =
      tq::TaskMessage::CreateWithDelay("add", nlohmann::json{}, 1000);
  broker_a.Enqueue(task);

  const std::int64_t promote_at = *task.run_at_ms;
  broker_a.PromoteDueTasks(promote_at);
  broker_b.PromoteDueTasks(promote_at);

  REQUIRE(redis.zcard(tq::redis_keys::kDelayed) == 0);
  REQUIRE(TaskIsAccountedFor(redis, task.id.Value()));
  REQUIRE(redis.llen(tq::redis_keys::kPending) >= 1);
}

TEST_CASE("RedisBroker ack keeps status and result coherent", "[integration][atomicity]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  sw::redis::Redis redis(tq::testing::RedisUri());

  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  const tq::TaskId id = broker.Enqueue(task);
  REQUIRE(broker.Reserve().has_value());

  broker.Ack(id);

  const auto status = broker.GetStatus(id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kSucceeded);

  const auto result = broker.GetTaskResult(id);
  REQUIRE(result.Ok());
  REQUIRE(result.Value().success);

  REQUIRE_FALSE(redis.hexists(tq::redis_keys::kRunning, id.Value()));
  REQUIRE(redis.hexists(tq::redis_keys::kResults, id.Value()));
  REQUIRE_FALSE(redis.hexists(tq::redis_keys::kFailures, id.Value()));
}

TEST_CASE("RedisBroker retry dead task requeues and clears failure reason",
          "[integration][atomicity]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  sw::redis::Redis redis(tq::testing::RedisUri());

  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  broker.Fail(reserved->id, "handler crashed");
  REQUIRE(DeadContainsTaskId(redis, reserved->id.Value()));

  const auto retried = broker.RetryDeadTask(reserved->id);
  REQUIRE(retried.Ok());
  REQUIRE_FALSE(DeadContainsTaskId(redis, reserved->id.Value()));
  REQUIRE(PendingContainsTaskId(redis, reserved->id.Value()));
  REQUIRE_FALSE(redis.hexists(tq::redis_keys::kFailures, reserved->id.Value()));

  const auto status = broker.GetStatus(reserved->id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kPending);
}

TEST_CASE("RedisBroker retry dead task returns error for missing id",
          "[integration][atomicity]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());

  const auto parsed_id = tq::TaskId::Parse("550e8400-e29b-41d4-a716-446655440000");
  REQUIRE(parsed_id.Ok());

  const auto retried = broker.RetryDeadTask(parsed_id.Value());
  REQUIRE_FALSE(retried.Ok());
  REQUIRE(retried.Error().find("dead task not found") != std::string::npos);
}

TEST_CASE("RedisBroker enqueue writes status atomically for immediate tasks",
          "[integration][atomicity]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  sw::redis::Redis redis(tq::testing::RedisUri());

  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  const tq::TaskId id = broker.Enqueue(task);

  REQUIRE(redis.llen(tq::redis_keys::kPending) == 1);
  REQUIRE(redis.hexists(tq::redis_keys::kStatus, id.Value()));
  const auto status = broker.GetStatus(id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kPending);
}

TEST_CASE("RedisBroker enqueue writes status atomically for delayed tasks",
          "[integration][atomicity]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  sw::redis::Redis redis(tq::testing::RedisUri());

  const tq::TaskMessage task =
      tq::TaskMessage::CreateWithDelay("add", nlohmann::json{}, 5000);
  broker.Enqueue(task);

  REQUIRE(redis.zcard(tq::redis_keys::kDelayed) == 1);
  REQUIRE(redis.hexists(tq::redis_keys::kStatus, task.id.Value()));
  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kPending);
}

#endif  // TASKQUEUE_HAS_REDIS && TASKQUEUE_ENABLE_INTEGRATION_TESTS

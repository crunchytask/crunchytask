#include "fake_broker.h"

#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"
#include "taskqueue/worker.h"
#include "taskqueue/worker_heartbeat.h"

namespace {

tq::TaskHandler MakeSlowHandler() {
  return [](const nlohmann::json& /*payload*/) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return tq::TaskResult::Success(nlohmann::json::object());
  };
}

tq::WorkerHeartbeat MakeSampleHeartbeat() {
  tq::WorkerHeartbeat heartbeat;
  heartbeat.worker_id = "550e8400-e29b-41d4-a716-446655440000";
  heartbeat.hostname = "test-host";
  heartbeat.pid = 4242;
  heartbeat.started_at_ms = 1717000000000;
  heartbeat.last_seen_ms = 1717000005000;
  heartbeat.concurrency = 4;
  heartbeat.currently_running = 1;
  return heartbeat;
}

}  // namespace

TEST_CASE("FakeBroker stores and lists worker heartbeats", "[unit][heartbeat]") {
  tq::testing::FakeBroker broker;
  const tq::WorkerHeartbeat heartbeat = MakeSampleHeartbeat();

  broker.UpsertWorkerHeartbeat(heartbeat, 30);
  const auto workers = broker.ListWorkers();

  REQUIRE(workers.size() == 1);
  REQUIRE(workers.front() == heartbeat);
}

TEST_CASE("FakeBroker drops expired worker heartbeats", "[unit][heartbeat]") {
  tq::testing::FakeBroker broker;
  const tq::WorkerHeartbeat heartbeat = MakeSampleHeartbeat();

  broker.UpsertWorkerHeartbeat(heartbeat, 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  REQUIRE(broker.ListWorkers().empty());
}

TEST_CASE("Worker publishes heartbeat while running", "[unit][heartbeat]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker, 2);
  worker.SetPollInterval(std::chrono::milliseconds(5));
  worker.SetHeartbeatInterval(std::chrono::milliseconds(10));
  worker.RegisterTask("slow", MakeSlowHandler());

  std::thread worker_thread([&worker]() { worker.Run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  worker.Stop();
  worker_thread.join();

  const auto workers = broker.ListWorkers();
  REQUIRE(workers.size() == 1);
  REQUIRE(workers.front().worker_id == worker.WorkerId());
  REQUIRE(workers.front().concurrency == 2);
  REQUIRE(workers.front().started_at_ms > 0);
  REQUIRE(workers.front().last_seen_ms >= workers.front().started_at_ms);
  REQUIRE_FALSE(workers.front().hostname.empty());
  REQUIRE(workers.front().pid > 0);
}

#if defined(TASKQUEUE_HAS_REDIS) && TASKQUEUE_ENABLE_INTEGRATION_TESTS

#include <sw/redis++/redis++.h>

#include "integration_guard.h"
#include "taskqueue/redis_broker.h"
#include "taskqueue/redis_keys.h"

namespace {

void ClearWorkerKeys(const std::string& redis_uri) {
  sw::redis::Redis redis(redis_uri);
  std::vector<std::string> keys;
  redis.keys(std::string(tq::redis_keys::kWorkersPrefix) + "*",
             std::back_inserter(keys));
  if (!keys.empty()) {
    redis.del(keys.begin(), keys.end());
  }
}

}  // namespace

TEST_CASE("RedisBroker stores worker heartbeat with TTL", "[integration][heartbeat]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearWorkerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  const tq::WorkerHeartbeat heartbeat = MakeSampleHeartbeat();

  broker.UpsertWorkerHeartbeat(heartbeat, 30);
  const auto workers = broker.ListWorkers();

  REQUIRE(workers.size() == 1);
  REQUIRE(workers.front() == heartbeat);

  sw::redis::Redis redis(tq::testing::RedisUri());
  const auto ttl = redis.ttl(tq::redis_keys::WorkerKey(heartbeat.worker_id));
  REQUIRE(ttl > 0);
  REQUIRE(ttl <= 30);
}

#endif

#include "fake_broker.h"

#include <atomic>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "taskqueue/clock.h"
#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"
#include "taskqueue/task_status.h"
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

TEST_CASE("Worker executes registered add task", "[worker]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);
  worker.RegisterTask("add", MakeAddHandler());

  const tq::TaskMessage task =
      tq::TaskMessage::Create("add", nlohmann::json{{"a", 2}, {"b", 3}});
  broker.Enqueue(task);

  REQUIRE(worker.RunOnce());

  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kSucceeded);
  REQUIRE(broker.RunningCount() == 0);
}

TEST_CASE("Worker fails tasks when handler throws on invalid payload",
          "[worker]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);
  worker.RegisterTask("add", MakeAddHandler());

  tq::TaskMessage task =
      tq::TaskMessage::Create("add", nlohmann::json{{"a", 1}});
  task.retry_policy.max_retries = 0;
  broker.Enqueue(task);

  REQUIRE(worker.RunOnce());

  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kDead);
  const auto failure_reason = broker.GetFailureReason(task.id);
  REQUIRE(failure_reason.Ok());
  REQUIRE(failure_reason.Value().find("key 'b' not found") != std::string::npos);
  REQUIRE(broker.RunningCount() == 0);
}

TEST_CASE("Worker fails unknown tasks", "[worker]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);

  const tq::TaskMessage task =
      tq::TaskMessage::Create("missing", nlohmann::json{});
  broker.Enqueue(task);

  REQUIRE(worker.RunOnce());

  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kDead);
  const auto failure_reason = broker.GetFailureReason(task.id);
  REQUIRE(failure_reason.Ok());
  REQUIRE(failure_reason.Value() == "unknown task: missing");
}

TEST_CASE("Worker RunOnce returns false when queue is empty", "[worker]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);

  REQUIRE_FALSE(worker.RunOnce());
}

TEST_CASE("Worker retries failed tasks until success", "[worker]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);
  std::atomic<int> attempts{0};

  worker.RegisterTask("flaky", [&attempts](const nlohmann::json&) {
    if (attempts.fetch_add(1) == 0) {
      return tq::TaskResult::Failure("transient");
    }
    return tq::TaskResult::Success({{"ok", true}});
  });

  tq::TaskMessage task = tq::TaskMessage::Create("flaky", nlohmann::json{});
  task.retry_policy.max_retries = 2;
  task.retry_policy.base_delay_ms = 0;
  broker.Enqueue(task);

  REQUIRE(worker.RunOnce());
  REQUIRE(attempts.load() == 1);
  REQUIRE(broker.DelayedCount() == 1);
  const auto failure_reason = broker.GetFailureReason(task.id);
  REQUIRE(failure_reason.Ok());
  REQUIRE(failure_reason.Value() == "transient");

  broker.PromoteDueTasks(tq::NowUnixMs());
  REQUIRE(worker.RunOnce());

  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kSucceeded);
  REQUIRE(attempts.load() == 2);
}

TEST_CASE("Worker moves exhausted tasks to dead state", "[worker]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);

  worker.RegisterTask("always_fail", [](const nlohmann::json&) {
    return tq::TaskResult::Failure("permanent");
  });

  tq::TaskMessage task = tq::TaskMessage::Create("always_fail", nlohmann::json{});
  task.retry_policy.max_retries = 1;
  task.retry_policy.base_delay_ms = 0;
  broker.Enqueue(task);

  REQUIRE(worker.RunOnce());
  broker.PromoteDueTasks(tq::NowUnixMs());
  REQUIRE(worker.RunOnce());

  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kDead);
  const auto failure_reason = broker.GetFailureReason(task.id);
  REQUIRE(failure_reason.Ok());
  REQUIRE(failure_reason.Value() == "permanent");
}

TEST_CASE("Worker executes delayed task after it becomes due", "[worker][delayed]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);
  worker.RegisterTask("add", MakeAddHandler());

  const tq::TaskMessage task = tq::TaskMessage::CreateWithDelay(
      "add", nlohmann::json{{"a", 1}, {"b", 2}}, 1000);
  broker.Enqueue(task);

  REQUIRE_FALSE(worker.RunOnce());
  broker.PromoteDueTasks(*task.run_at_ms);
  REQUIRE(worker.RunOnce());

  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kSucceeded);
}

TEST_CASE("Worker processes tasks concurrently", "[worker]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker, 4);

  std::atomic<int> active{0};
  std::atomic<int> max_active{0};
  worker.RegisterTask(
      "track",
      [&active, &max_active](const nlohmann::json&) {
        const int current = active.fetch_add(1) + 1;
        int observed = max_active.load();
        while (current > observed &&
               !max_active.compare_exchange_weak(observed, current)) {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        active.fetch_sub(1);
        return tq::TaskResult::Success();
      });

  for (int i = 0; i < 8; ++i) {
    broker.Enqueue(tq::TaskMessage::Create("track", nlohmann::json{}));
  }

  worker.SetPollInterval(std::chrono::milliseconds(5));
  std::thread worker_thread([&worker]() { worker.Run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  worker.Stop();
  worker_thread.join();

  REQUIRE(max_active.load() > 1);
  REQUIRE(broker.RunningCount() == 0);
}

TEST_CASE("Worker stops gracefully", "[worker]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);
  worker.SetPollInterval(std::chrono::milliseconds(10));

  std::thread worker_thread([&worker]() { worker.Run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  worker.Stop();
  worker_thread.join();

  REQUIRE_FALSE(worker.IsRunning());
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

TEST_CASE("Worker executes task from Redis", "[integration][worker]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearBrokerKeys(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  tq::Worker worker(broker);
  worker.RegisterTask("add", MakeAddHandler());

  const tq::TaskMessage task =
      tq::TaskMessage::Create("add", nlohmann::json{{"a", 4}, {"b", 5}});
  broker.Enqueue(task);

  REQUIRE(worker.RunOnce());

  const auto status = broker.GetStatus(task.id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kSucceeded);
}

#endif

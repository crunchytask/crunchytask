#include "fake_broker.h"

#include <catch2/catch_test_macros.hpp>

#include "taskqueue/scheduler.h"
#include "taskqueue/task_message.h"
#include "taskqueue/worker.h"

TEST_CASE("RunSchedulerTick promotes due delayed tasks", "[unit][scheduler]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task =
      tq::TaskMessage::CreateWithDelay("add", nlohmann::json{}, 5000);
  broker.Enqueue(task);

  tq::RunSchedulerTick(broker, tq::Worker::kDefaultVisibilityTimeoutMs);
  REQUIRE(broker.PendingCount() == 0);
  REQUIRE(broker.DelayedCount() == 1);

  broker.PromoteDueTasks(*task.run_at_ms);
  tq::RunSchedulerTick(broker, tq::Worker::kDefaultVisibilityTimeoutMs);
  REQUIRE(broker.PendingCount() == 1);
  REQUIRE(broker.DelayedCount() == 0);
}

TEST_CASE("RunSchedulerTick does not reclaim fresh running tasks",
          "[unit][scheduler]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);

  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  tq::RunSchedulerTick(broker, 60'000);
  REQUIRE(broker.RunningCount() == 1);
  REQUIRE(broker.PendingCount() == 0);
}

#include "fake_broker.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "taskqueue/broker.h"
#include "taskqueue/clock.h"
#include "taskqueue/task_message.h"

TEST_CASE("FakeBroker enqueues and reserves tasks", "[broker]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task =
      tq::TaskMessage::Create("add", nlohmann::json{{"a", 1}, {"b", 2}});

  const tq::TaskId id = broker.Enqueue(task);
  REQUIRE(id == task.id);
  REQUIRE(broker.PendingCount() == 1);
  REQUIRE(broker.RunningCount() == 0);

  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  REQUIRE(reserved->id == task.id);
  REQUIRE(reserved->name == "add");
  REQUIRE(reserved->status == tq::TaskStatus::kRunning);
  REQUIRE(broker.PendingCount() == 0);
  REQUIRE(broker.RunningCount() == 1);
}

TEST_CASE("FakeBroker acknowledges completed tasks", "[broker]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  broker.Ack(reserved->id);
  REQUIRE(broker.RunningCount() == 0);

  const auto status = broker.GetStatus(reserved->id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kSucceeded);
}

TEST_CASE("FakeBroker retries failed tasks", "[broker]") {
  tq::testing::FakeBroker broker;
  tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  task.retry_policy.base_delay_ms = 0;
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  broker.Retry(*reserved, "transient error");
  REQUIRE(broker.RunningCount() == 0);
  REQUIRE(broker.PendingCount() == 0);
  REQUIRE(broker.DelayedCount() == 1);
  const auto failure_reason = broker.GetFailureReason(reserved->id);
  REQUIRE(failure_reason.Ok());
  REQUIRE(failure_reason.Value() == "transient error");

  const auto status = broker.GetStatus(reserved->id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kRetrying);

  broker.PromoteDueTasks(tq::NowUnixMs());
  REQUIRE(broker.PendingCount() == 1);
  REQUIRE(broker.DelayedCount() == 0);

  const auto retried = broker.Reserve();
  REQUIRE(retried.has_value());
  REQUIRE(retried->retry_count == 1);
  REQUIRE(retried->last_error == "transient error");
  REQUIRE(retried->status == tq::TaskStatus::kRunning);
}

TEST_CASE("FakeBroker moves exhausted tasks to dead state", "[broker]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task = tq::TaskMessage::Create("add", nlohmann::json{});
  broker.Enqueue(task);
  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());

  broker.Fail(reserved->id, "handler crashed");
  REQUIRE(broker.RunningCount() == 0);

  const auto status = broker.GetStatus(reserved->id);
  REQUIRE(status.Ok());
  REQUIRE(status.Value() == tq::TaskStatus::kDead);
  const auto failure_reason = broker.GetFailureReason(reserved->id);
  REQUIRE(failure_reason.Ok());
  REQUIRE(failure_reason.Value() == "handler crashed");
}

TEST_CASE("FakeBroker GetStatus reports missing tasks", "[broker]") {
  tq::testing::FakeBroker broker;
  const auto id = tq::TaskId::Parse("550e8400-e29b-41d4-a716-446655440000");
  REQUIRE(id.Ok());

  const auto status = broker.GetStatus(id.Value());
  REQUIRE_FALSE(status.Ok());
  REQUIRE(status.Error().find("task not found") != std::string::npos);
}

TEST_CASE("Broker interface is mockable", "[broker]") {
  tq::testing::FakeBroker fake;
  tq::Broker& broker = fake;

  const tq::TaskMessage task = tq::TaskMessage::Create("ping", nlohmann::json{});
  broker.Enqueue(task);

  const auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  broker.Ack(reserved->id);
}

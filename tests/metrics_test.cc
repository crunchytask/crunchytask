#include "fake_broker.h"

#include <catch2/catch_test_macros.hpp>

#include "taskqueue/metrics.h"
#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"
#include "taskqueue/worker.h"

namespace {

tq::TaskHandler MakeFailingHandler() {
  return [](const nlohmann::json& /*payload*/) {
    return tq::TaskResult::Failure("boom");
  };
}

}  // namespace

TEST_CASE("MetricsCollector tracks counters and histograms", "[unit][metrics]") {
  tq::MetricsCollector collector;
  collector.IncrementCounter(tq::metrics_names::kTasksEnqueuedTotal, 2);
  collector.SetGauge(tq::metrics_names::kQueueDepth, 5);
  collector.ObserveDurationMs(tq::metrics_names::kTaskExecutionDurationMs, 10);
  collector.ObserveDurationMs(tq::metrics_names::kTaskExecutionDurationMs, 30);

  const tq::MetricsSnapshot snapshot = collector.Snapshot();
  REQUIRE(snapshot.counters.at(tq::metrics_names::kTasksEnqueuedTotal) == 2);
  REQUIRE(snapshot.gauges.at(tq::metrics_names::kQueueDepth) == 5);

  const auto& histogram =
      snapshot.histograms.at(tq::metrics_names::kTaskExecutionDurationMs);
  REQUIRE(histogram.count == 2);
  REQUIRE(histogram.sum_ms == 40);
  REQUIRE(histogram.max_ms == 30);
}

TEST_CASE("FormatMetricsPrometheus renders metric types", "[unit][metrics]") {
  tq::MetricsCollector collector;
  collector.IncrementCounter(tq::metrics_names::kTasksCompletedTotal);
  collector.SetGauge(tq::metrics_names::kRunningTasks, 1);
  collector.ObserveDurationMs(tq::metrics_names::kTaskExecutionDurationMs, 12);

  const std::string output =
      tq::FormatMetricsPrometheus(collector.Snapshot());
  REQUIRE(output.find("# TYPE tasks_completed_total counter") != std::string::npos);
  REQUIRE(output.find("tasks_completed_total 1") != std::string::npos);
  REQUIRE(output.find("# TYPE running_tasks gauge") != std::string::npos);
  REQUIRE(output.find("task_execution_duration_ms_count 1") != std::string::npos);
}

TEST_CASE("FakeBroker increments enqueue and completion counters", "[unit][metrics]") {
  tq::testing::FakeBroker broker;
  const tq::TaskMessage task =
      tq::TaskMessage::Create("add", nlohmann::json{{"a", 1}, {"b", 2}});

  broker.Enqueue(task);
  auto reserved = broker.Reserve();
  REQUIRE(reserved.has_value());
  broker.Ack(reserved->id);

  const tq::MetricsSnapshot snapshot = broker.CollectMetrics();
  REQUIRE(snapshot.counters.at(tq::metrics_names::kTasksEnqueuedTotal) == 1);
  REQUIRE(snapshot.counters.at(tq::metrics_names::kTasksCompletedTotal) == 1);
  REQUIRE(snapshot.gauges.at(tq::metrics_names::kQueueDepth) == 0);
}

TEST_CASE("FakeBroker increments retry and dead-letter counters", "[unit][metrics]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);
  worker.RegisterTask("fail", MakeFailingHandler());

  tq::TaskMessage task = tq::TaskMessage::Create("fail", nlohmann::json::object());
  task.retry_policy.max_retries = 0;
  broker.Enqueue(task);

  REQUIRE(worker.RunOnce());

  const tq::MetricsSnapshot snapshot = broker.CollectMetrics();
  REQUIRE(snapshot.counters.at(tq::metrics_names::kTasksFailedTotal) == 1);
  REQUIRE(snapshot.counters.at(tq::metrics_names::kTasksDeadLetteredTotal) == 1);
  REQUIRE(snapshot.histograms.at(tq::metrics_names::kTaskExecutionDurationMs)
              .count == 1);
}

#if defined(TASKQUEUE_HAS_REDIS) && TASKQUEUE_ENABLE_INTEGRATION_TESTS

#include <sw/redis++/redis++.h>

#include "integration_guard.h"
#include "taskqueue/redis_broker.h"
#include "taskqueue/redis_keys.h"

namespace {

void ClearMetricsKey(const std::string& redis_uri) {
  sw::redis::Redis redis(redis_uri);
  redis.del(tq::redis_keys::kMetrics);
}

}  // namespace

TEST_CASE("RedisBroker persists metrics for CLI collection",
          "[integration][metrics]") {
  if (tq::testing::SkipIfNoRedis()) {
    return;
  }

  ClearMetricsKey(tq::testing::RedisUri());
  tq::RedisBroker broker(tq::testing::RedisUri());
  const tq::TaskMessage task =
      tq::TaskMessage::Create("add", nlohmann::json{{"a", 1}});

  broker.Enqueue(task);
  broker.RecordDurationMs(tq::metrics_names::kTaskExecutionDurationMs, 7);

  const tq::MetricsSnapshot snapshot = broker.CollectMetrics();
  REQUIRE(snapshot.counters.at(tq::metrics_names::kTasksEnqueuedTotal) == 1);
  REQUIRE(snapshot.histograms.at(tq::metrics_names::kTaskExecutionDurationMs)
              .count == 1);
  REQUIRE(snapshot.histograms.at(tq::metrics_names::kTaskExecutionDurationMs)
              .sum_ms == 7);
}

#endif

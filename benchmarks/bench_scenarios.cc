#include "bench_scenarios.h"

#include "bench_harness.h"

#include <thread>

#include "taskqueue/broker_stats.h"
#include "taskqueue/clock.h"
#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"
#include "taskqueue/worker.h"

#ifdef TASKQUEUE_HAS_REDIS
#include <sw/redis++/redis++.h>

#include "taskqueue/redis_broker.h"
#include "taskqueue/redis_keys.h"
#endif

namespace tq {
namespace bench {

namespace {

nlohmann::json SkippedResult(const std::string& name, const std::string& reason) {
  nlohmann::json result = MakeResult(name, "skipped");
  SetMetric(result, "reason", reason);
  return result;
}

#ifdef TASKQUEUE_HAS_REDIS

bool RedisAvailable(const std::string& redis_uri) {
  return RedisBroker::Ping(redis_uri);
}

void ClearBenchmarkKeys(const std::string& redis_uri) {
  sw::redis::Redis redis(redis_uri);
  redis.del({redis_keys::kPending, redis_keys::kDelayed, redis_keys::kRunning,
             redis_keys::kStatus, redis_keys::kResults, redis_keys::kDead,
             redis_keys::kFailures, redis_keys::kMetrics});

  std::vector<std::string> worker_keys;
  redis.keys(std::string(redis_keys::kWorkersPrefix) + "*",
             std::back_inserter(worker_keys));
  if (!worker_keys.empty()) {
    redis.del(worker_keys.begin(), worker_keys.end());
  }
}

TaskMessage MakeBenchTask() {
  return TaskMessage::Create("bench_noop", nlohmann::json::object());
}

TaskHandler MakeNoopHandler() {
  return [](const nlohmann::json& /*payload*/) {
    return TaskResult::Success(nlohmann::json::object());
  };
}

nlohmann::json BenchmarkEnqueueThroughput(const BenchConfig& config) {
  constexpr const char kName[] = "enqueue_throughput";
  if (!RedisAvailable(config.redis_uri)) {
    return SkippedResult(kName, "redis unavailable");
  }

  ClearBenchmarkKeys(config.redis_uri);
  RedisBroker broker(config.redis_uri);

  for (std::int64_t i = 0; i < config.warmup; ++i) {
    broker.Enqueue(MakeBenchTask());
  }
  ClearBenchmarkKeys(config.redis_uri);

  const auto start = Clock::now();
  for (std::int64_t i = 0; i < config.iterations; ++i) {
    broker.Enqueue(MakeBenchTask());
  }
  const auto end = Clock::now();

  const double duration_ms = DurationMs(start, end);
  nlohmann::json result = MakeResult(kName, "ok");
  SetMetric(result, "backend", "redis");
  SetMetric(result, "iterations", config.iterations);
  SetMetric(result, "duration_ms", duration_ms);
  SetMetric(result, "throughput_per_sec",
            ThroughputPerSec(config.iterations, duration_ms));
  return result;
}

nlohmann::json BenchmarkWorkerCompletionThroughput(const BenchConfig& config) {
  constexpr const char kName[] = "worker_completion_throughput";
  if (!RedisAvailable(config.redis_uri)) {
    return SkippedResult(kName, "redis unavailable");
  }

  ClearBenchmarkKeys(config.redis_uri);
  RedisBroker broker(config.redis_uri);
  Worker worker(broker, 4);
  worker.SetPollInterval(std::chrono::milliseconds(1));
  worker.RegisterTask("bench_noop", MakeNoopHandler());

  for (std::int64_t i = 0; i < config.iterations; ++i) {
    broker.Enqueue(MakeBenchTask());
  }

  const auto start = Clock::now();
  std::thread worker_thread([&worker]() { worker.Run(); });

  const auto deadline = Clock::now() + std::chrono::seconds(120);
  bool completed = false;
  while (Clock::now() < deadline) {
    const BrokerStats stats = broker.GetStats();
    if (stats.pending_count == 0 && stats.running_count == 0 &&
        stats.delayed_count == 0) {
      completed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  worker.Stop();
  worker_thread.join();
  const auto end = Clock::now();

  if (!completed) {
    return SkippedResult(kName, "worker did not drain queue before timeout");
  }

  const double duration_ms = DurationMs(start, end);
  nlohmann::json result = MakeResult(kName, "ok");
  SetMetric(result, "backend", "redis");
  SetMetric(result, "iterations", config.iterations);
  SetMetric(result, "worker_concurrency", static_cast<std::int64_t>(4));
  SetMetric(result, "duration_ms", duration_ms);
  SetMetric(result, "throughput_per_sec",
            ThroughputPerSec(config.iterations, duration_ms));
  return result;
}

nlohmann::json BenchmarkRetryOverhead(const BenchConfig& config) {
  constexpr const char kName[] = "retry_overhead";
  if (!RedisAvailable(config.redis_uri)) {
    return SkippedResult(kName, "redis unavailable");
  }

  ClearBenchmarkKeys(config.redis_uri);
  RedisBroker broker(config.redis_uri);

  const auto run_ack_path = [&](const std::int64_t count) {
    ClearBenchmarkKeys(config.redis_uri);
    const auto start = Clock::now();
    for (std::int64_t i = 0; i < count; ++i) {
      const TaskId id = broker.Enqueue(MakeBenchTask());
      const auto reserved = broker.Reserve();
      if (!reserved.has_value()) {
        break;
      }
      broker.Ack(reserved->id);
      (void)id;
    }
    return DurationMs(start, Clock::now());
  };

  const auto run_retry_path = [&](const std::int64_t count) {
    ClearBenchmarkKeys(config.redis_uri);
    const auto start = Clock::now();
    for (std::int64_t i = 0; i < count; ++i) {
      TaskMessage task = MakeBenchTask();
      task.retry_policy.base_delay_ms = 0;
      broker.Enqueue(task);
      const auto reserved = broker.Reserve();
      if (!reserved.has_value()) {
        break;
      }
      broker.Retry(*reserved, "bench retry");
    }
    return DurationMs(start, Clock::now());
  };

  const std::int64_t samples = std::max<std::int64_t>(50, config.iterations / 10);
  const double ack_duration_ms = run_ack_path(samples);
  const double retry_duration_ms = run_retry_path(samples);

  nlohmann::json result = MakeResult(kName, "ok");
  SetMetric(result, "backend", "redis");
  SetMetric(result, "iterations", samples);
  SetMetric(result, "ack_duration_ms", ack_duration_ms);
  SetMetric(result, "retry_duration_ms", retry_duration_ms);
  SetMetric(result, "overhead_per_op_ms",
            (retry_duration_ms - ack_duration_ms) /
                static_cast<double>(samples));
  SetMetric(result, "retry_to_ack_ratio",
            ack_duration_ms > 0.0 ? retry_duration_ms / ack_duration_ms : 0.0);
  return result;
}

nlohmann::json BenchmarkDelayedSchedulingLatency(const BenchConfig& config) {
  constexpr const char kName[] = "delayed_task_scheduling_latency";
  if (!RedisAvailable(config.redis_uri)) {
    return SkippedResult(kName, "redis unavailable");
  }

  ClearBenchmarkKeys(config.redis_uri);
  RedisBroker broker(config.redis_uri);

  double total_latency_ms = 0.0;
  const std::int64_t samples = std::max<std::int64_t>(20, config.iterations / 25);

  for (std::int64_t i = 0; i < samples; ++i) {
    const std::int64_t delay_ms = 5;
    const std::int64_t due_at_ms = NowUnixMs() + delay_ms;
    TaskMessage task =
        TaskMessage::CreateWithDelay("bench_noop", nlohmann::json::object(), delay_ms);
    task.run_at_ms = due_at_ms;
    broker.Enqueue(task);

    const auto wait_start = Clock::now();
    bool promoted = false;
    while (DurationMs(wait_start, Clock::now()) < 2000.0) {
      const std::int64_t now_ms = NowUnixMs();
      if (now_ms >= due_at_ms) {
        broker.PromoteDueTasks(now_ms);
        if (broker.GetStats().pending_count > 0) {
          total_latency_ms += static_cast<double>(now_ms - due_at_ms);
          promoted = true;
          break;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!promoted) {
      nlohmann::json result = MakeResult(kName, "error");
      SetMetric(result, "reason", "delayed task was not promoted before timeout");
      return result;
    }

    const auto reserved = broker.Reserve();
    if (reserved.has_value()) {
      broker.Ack(reserved->id);
    }
    ClearBenchmarkKeys(config.redis_uri);
  }

  nlohmann::json result = MakeResult(kName, "ok");
  SetMetric(result, "backend", "redis");
  SetMetric(result, "iterations", samples);
  SetMetric(result, "mean_latency_ms", total_latency_ms / static_cast<double>(samples));
  SetMetric(result, "target_delay_ms", static_cast<std::int64_t>(5));
  return result;
}

nlohmann::json BenchmarkStaleReclaimLatency(const BenchConfig& config) {
  constexpr const char kName[] = "stale_reclaim_latency";
  if (!RedisAvailable(config.redis_uri)) {
    return SkippedResult(kName, "redis unavailable");
  }

  ClearBenchmarkKeys(config.redis_uri);
  RedisBroker broker(config.redis_uri);

  double total_reclaim_ms = 0.0;
  const std::int64_t samples = std::max<std::int64_t>(20, config.iterations / 25);

  for (std::int64_t i = 0; i < samples; ++i) {
    const TaskId id = broker.Enqueue(MakeBenchTask());
    const auto reserved = broker.Reserve();
    if (!reserved.has_value() || !reserved->reserved_at_ms.has_value()) {
      continue;
    }

    const std::int64_t reclaim_at_ms =
        *reserved->reserved_at_ms + config.visibility_timeout_ms + 1;
    const auto start = Clock::now();
    const int reclaimed =
        broker.ReclaimStaleTasks(reclaim_at_ms, config.visibility_timeout_ms);
    const double reclaim_ms = DurationMs(start, Clock::now());
    total_reclaim_ms += reclaim_ms;

    if (reclaimed != 1) {
      nlohmann::json result = MakeResult(kName, "error");
      SetMetric(result, "reason", "expected one reclaimed task");
      SetMetric(result, "task_id", id.Value());
      return result;
    }

    ClearBenchmarkKeys(config.redis_uri);
  }

  nlohmann::json result = MakeResult(kName, "ok");
  SetMetric(result, "backend", "redis");
  SetMetric(result, "iterations", samples);
  SetMetric(result, "visibility_timeout_ms", config.visibility_timeout_ms);
  SetMetric(result, "mean_reclaim_ms",
            total_reclaim_ms / static_cast<double>(samples));
  return result;
}

#else

nlohmann::json RedisDisabledResult(const std::string& name) {
  return SkippedResult(name, "redis support disabled at build time");
}

nlohmann::json BenchmarkEnqueueThroughput(const BenchConfig& /*config*/) {
  return RedisDisabledResult("enqueue_throughput");
}

nlohmann::json BenchmarkWorkerCompletionThroughput(const BenchConfig& /*config*/) {
  return RedisDisabledResult("worker_completion_throughput");
}

nlohmann::json BenchmarkRetryOverhead(const BenchConfig& /*config*/) {
  return RedisDisabledResult("retry_overhead");
}

nlohmann::json BenchmarkDelayedSchedulingLatency(const BenchConfig& /*config*/) {
  return RedisDisabledResult("delayed_task_scheduling_latency");
}

nlohmann::json BenchmarkStaleReclaimLatency(const BenchConfig& /*config*/) {
  return RedisDisabledResult("stale_reclaim_latency");
}

#endif

}  // namespace

std::vector<nlohmann::json> RunAllBenchmarks(const BenchConfig& config) {
  return {BenchmarkEnqueueThroughput(config),
          BenchmarkWorkerCompletionThroughput(config),
          BenchmarkRetryOverhead(config),
          BenchmarkDelayedSchedulingLatency(config),
          BenchmarkStaleReclaimLatency(config)};
}

}  // namespace bench
}  // namespace tq

#ifndef TASKQUEUE_METRICS_H_
#define TASKQUEUE_METRICS_H_

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace tq {

namespace metrics_names {

inline constexpr const char kTasksEnqueuedTotal[] = "tasks_enqueued_total";
inline constexpr const char kTasksCompletedTotal[] = "tasks_completed_total";
inline constexpr const char kTasksFailedTotal[] = "tasks_failed_total";
inline constexpr const char kTasksRetriedTotal[] = "tasks_retried_total";
inline constexpr const char kTasksDeadLetteredTotal[] = "tasks_dead_lettered_total";
inline constexpr const char kRedisOperationErrorsTotal[] = "redis_operation_errors_total";

inline constexpr const char kQueueDepth[] = "queue_depth";
inline constexpr const char kDelayedDepth[] = "delayed_depth";
inline constexpr const char kRunningTasks[] = "running_tasks";
inline constexpr const char kWorkerThreadsActive[] = "worker_threads_active";

inline constexpr const char kTaskExecutionDurationMs[] = "task_execution_duration_ms";

}  // namespace metrics_names

struct DurationHistogram {
  std::int64_t count = 0;
  std::int64_t sum_ms = 0;
  std::int64_t max_ms = 0;
};

struct MetricsSnapshot {
  std::unordered_map<std::string, std::int64_t> counters;
  std::unordered_map<std::string, std::int64_t> gauges;
  std::unordered_map<std::string, DurationHistogram> histograms;
};

class MetricsCollector {
 public:
  void IncrementCounter(const std::string& name, std::int64_t delta = 1);
  void SetGauge(const std::string& name, std::int64_t value);
  void ObserveDurationMs(const std::string& name, std::int64_t duration_ms);

  MetricsSnapshot Snapshot() const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::int64_t> counters_;
  std::unordered_map<std::string, std::int64_t> gauges_;
  std::unordered_map<std::string, DurationHistogram> histograms_;
};

std::string FormatMetricsPlainText(const MetricsSnapshot& snapshot);
std::string FormatMetricsPrometheus(const MetricsSnapshot& snapshot);

}  // namespace tq

#endif  // TASKQUEUE_METRICS_H_

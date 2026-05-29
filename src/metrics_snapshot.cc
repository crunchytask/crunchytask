#include "taskqueue/metrics_snapshot.h"

#include "taskqueue/metrics.h"

namespace tq {

namespace {

void ApplyBrokerGauges(MetricsSnapshot& snapshot, const BrokerStats& stats,
                       const std::vector<WorkerHeartbeat>& workers) {
  snapshot.gauges[metrics_names::kQueueDepth] =
      static_cast<std::int64_t>(stats.pending_count);
  snapshot.gauges[metrics_names::kDelayedDepth] =
      static_cast<std::int64_t>(stats.delayed_count);
  snapshot.gauges[metrics_names::kRunningTasks] =
      static_cast<std::int64_t>(stats.running_count);

  std::size_t active_threads = 0;
  for (const WorkerHeartbeat& worker : workers) {
    active_threads += worker.currently_running;
  }
  snapshot.gauges[metrics_names::kWorkerThreadsActive] =
      static_cast<std::int64_t>(active_threads);
}

}  // namespace

void ApplyLiveGauges(MetricsSnapshot& snapshot, const Broker& broker) {
  ApplyBrokerGauges(snapshot, broker.GetStats(), broker.ListWorkers());
}

MetricsSnapshot CollectMetrics(const Broker& broker) {
  return broker.CollectMetrics();
}

}  // namespace tq

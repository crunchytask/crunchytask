#ifndef TASKQUEUE_METRICS_SNAPSHOT_H_
#define TASKQUEUE_METRICS_SNAPSHOT_H_

#include "taskqueue/broker.h"
#include "taskqueue/metrics.h"

namespace tq {

void ApplyLiveGauges(MetricsSnapshot& snapshot, const Broker& broker);
MetricsSnapshot CollectMetrics(const Broker& broker);

}  // namespace tq

#endif  // TASKQUEUE_METRICS_SNAPSHOT_H_

#ifndef TASKQUEUE_BROKER_H_
#define TASKQUEUE_BROKER_H_

#include <optional>
#include <string>
#include <vector>

#include "taskqueue/metrics.h"
#include "taskqueue/parse_result.h"
#include "taskqueue/broker_stats.h"
#include "taskqueue/task_id.h"
#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"
#include "taskqueue/task_status.h"
#include "taskqueue/worker_heartbeat.h"

namespace tq {

class Broker {
 public:
  virtual ~Broker() = default;

  virtual TaskId Enqueue(const TaskMessage& task) = 0;
  virtual void PromoteDueTasks(std::int64_t now_ms) = 0;
  virtual int ReclaimStaleTasks(std::int64_t now_ms,
                                std::int64_t visibility_timeout_ms) = 0;
  virtual std::optional<TaskMessage> Reserve() = 0;
  virtual void Ack(const TaskId& id) = 0;
  virtual void Retry(const TaskMessage& task, const std::string& reason) = 0;
  virtual void Fail(const TaskId& id, const std::string& reason) = 0;
  virtual ParseResult<TaskStatus> GetStatus(const TaskId& id) const = 0;
  virtual std::vector<TaskMessage> ListDeadTasks() const = 0;
  virtual ParseResult<std::string> GetFailureReason(const TaskId& id) const = 0;
  virtual ParseResult<TaskId> RetryDeadTask(const TaskId& id) = 0;
  virtual BrokerStats GetStats() const = 0;
  virtual ParseResult<TaskResult> GetTaskResult(const TaskId& id) const = 0;
  virtual void UpsertWorkerHeartbeat(const WorkerHeartbeat& heartbeat,
                                     std::int64_t ttl_seconds) = 0;
  virtual std::vector<WorkerHeartbeat> ListWorkers() const = 0;
  virtual void RecordCounter(const std::string& name, std::int64_t delta = 1) = 0;
  virtual void RecordDurationMs(const std::string& name, std::int64_t duration_ms) = 0;
  virtual MetricsSnapshot CollectMetrics() const = 0;
};

}  // namespace tq

#endif  // TASKQUEUE_BROKER_H_

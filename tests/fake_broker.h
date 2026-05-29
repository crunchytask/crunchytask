#ifndef TASKQUEUE_TESTS_FAKE_BROKER_H_
#define TASKQUEUE_TESTS_FAKE_BROKER_H_

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "taskqueue/broker.h"
#include "taskqueue/broker_stats.h"
#include "taskqueue/task_result.h"
#include "taskqueue/worker_heartbeat.h"

namespace tq {
namespace testing {

class FakeBroker final : public Broker {
 public:
  TaskId Enqueue(const TaskMessage& task) override;
  void PromoteDueTasks(std::int64_t now_ms) override;
  int ReclaimStaleTasks(std::int64_t now_ms,
                       std::int64_t visibility_timeout_ms) override;
  std::optional<TaskMessage> Reserve() override;
  void Ack(const TaskId& id) override;
  void Retry(const TaskMessage& task, const std::string& reason) override;
  void Fail(const TaskId& id, const std::string& reason) override;
  ParseResult<TaskStatus> GetStatus(const TaskId& id) const override;
  std::vector<TaskMessage> ListDeadTasks() const override;
  ParseResult<std::string> GetFailureReason(const TaskId& id) const override;
  ParseResult<TaskId> RetryDeadTask(const TaskId& id) override;
  BrokerStats GetStats() const override;
  ParseResult<TaskResult> GetTaskResult(const TaskId& id) const override;
  void UpsertWorkerHeartbeat(const WorkerHeartbeat& heartbeat,
                             std::int64_t ttl_seconds) override;
  std::vector<WorkerHeartbeat> ListWorkers() const override;

  std::size_t PendingCount() const { return pending_.size(); }
  std::size_t DelayedCount() const { return delayed_.size(); }
  std::size_t RunningCount() const { return running_.size(); }
  std::size_t DeadCount() const { return dead_.size(); }

 private:
  struct DelayedEntry {
    std::int64_t run_at_ms;
    TaskMessage task;
  };

  void AddDelayedTask(TaskMessage task, std::int64_t run_at_ms);

  std::deque<TaskMessage> pending_;
  std::vector<DelayedEntry> delayed_;
  std::unordered_map<std::string, TaskMessage> running_;
  std::unordered_map<std::string, TaskMessage> dead_;
  std::unordered_map<std::string, TaskStatus> statuses_;
  std::unordered_map<std::string, std::string> failure_reasons_;
  std::unordered_map<std::string, TaskResult> results_;

  struct StoredWorkerHeartbeat {
    WorkerHeartbeat heartbeat;
    std::int64_t expires_at_ms = 0;
  };

  std::unordered_map<std::string, StoredWorkerHeartbeat> workers_;
};

}  // namespace testing
}  // namespace tq

#endif  // TASKQUEUE_TESTS_FAKE_BROKER_H_

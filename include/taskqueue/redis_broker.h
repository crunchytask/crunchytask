#ifndef TASKQUEUE_REDIS_BROKER_H_
#define TASKQUEUE_REDIS_BROKER_H_

#include <memory>
#include <string>

#include "taskqueue/broker.h"

namespace sw {
namespace redis {
class Redis;
}  // namespace redis
}  // namespace sw

namespace tq {

class RedisBroker final : public Broker {
 public:
  explicit RedisBroker(std::string redis_uri);
  ~RedisBroker() override;

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

  static bool Ping(const std::string& redis_uri);

 private:
  mutable std::unique_ptr<sw::redis::Redis> redis_;
};

}  // namespace tq

#endif  // TASKQUEUE_REDIS_BROKER_H_

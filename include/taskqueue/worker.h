#ifndef TASKQUEUE_WORKER_H_
#define TASKQUEUE_WORKER_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

#include "taskqueue/broker.h"
#include "taskqueue/task_handler.h"
#include "taskqueue/task_registry.h"
#include "taskqueue/thread_pool.h"
#include "taskqueue/worker_heartbeat.h"

namespace tq {

class Worker {
 public:
  Worker(Broker& broker, std::size_t concurrency = 1);

  void RegisterTask(std::string name, TaskHandler handler);
  void Run();
  void Stop();
  bool RunOnce();
  bool IsRunning() const { return running_.load(); }

  void SetPollInterval(std::chrono::milliseconds interval);
  void SetVisibilityTimeout(std::chrono::milliseconds timeout);
  void SetHeartbeatInterval(std::chrono::milliseconds interval);
  std::size_t Concurrency() const { return thread_pool_->ThreadCount(); }
  const std::string& WorkerId() const { return worker_id_; }

  static constexpr std::int64_t kDefaultVisibilityTimeoutMs = 30000;
  static constexpr std::int64_t kDefaultHeartbeatTtlSeconds =
      kDefaultWorkerHeartbeatTtlSeconds;

 private:
  void ProcessTask(const TaskMessage& task);
  bool CanAcceptTask() const;
  void MaybePublishHeartbeat();

  Broker& broker_;
  TaskRegistry registry_;
  std::unique_ptr<ThreadPool> thread_pool_;
  std::mutex broker_mutex_;
  std::atomic<bool> running_{false};
  std::string worker_id_;
  std::string hostname_;
  std::int64_t pid_ = 0;
  std::int64_t started_at_ms_ = 0;
  std::chrono::milliseconds poll_interval_{100};
  std::chrono::milliseconds visibility_timeout_{kDefaultVisibilityTimeoutMs};
  std::chrono::milliseconds heartbeat_interval_{kDefaultWorkerHeartbeatIntervalMs};
  std::int64_t heartbeat_ttl_seconds_{kDefaultWorkerHeartbeatTtlSeconds};
  std::chrono::steady_clock::time_point last_heartbeat_at_{};
};

}  // namespace tq

#endif  // TASKQUEUE_WORKER_H_

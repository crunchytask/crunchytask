#include "taskqueue/worker.h"

#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <utility>

#include <spdlog/spdlog.h>

#include "taskqueue/clock.h"
#include "taskqueue/metrics.h"
#include "taskqueue/retry_policy.h"
#include "taskqueue/runtime_config.h"
#include "taskqueue/scheduler.h"
#include "taskqueue/task_id.h"

namespace tq {

namespace {

void ThrowIfInvalid(const ConfigValidation& validation) {
  if (!validation.Ok()) {
    throw std::invalid_argument(validation.Error());
  }
}

}  // namespace

Worker::Worker(Broker& broker, std::size_t concurrency) : broker_(broker) {
  ThrowIfInvalid(ValidateWorkerConcurrency(concurrency));
  ThrowIfInvalid(ValidatePollInterval(poll_interval_));
  ThrowIfInvalid(ValidateVisibilityTimeout(visibility_timeout_));
  ThrowIfInvalid(ValidatePollInterval(heartbeat_interval_));
  worker_id_ = TaskId::Generate().Value();
  hostname_ = ReadHostname();
  pid_ = static_cast<std::int64_t>(getpid());
  thread_pool_ = std::make_unique<ThreadPool>(concurrency);
}

void Worker::RegisterTask(std::string name, TaskHandler handler) {
  registry_.RegisterTask(std::move(name), std::move(handler));
}

void Worker::SetPollInterval(std::chrono::milliseconds interval) {
  ThrowIfInvalid(ValidatePollInterval(interval));
  poll_interval_ = interval;
}

void Worker::SetVisibilityTimeout(std::chrono::milliseconds timeout) {
  ThrowIfInvalid(ValidateVisibilityTimeout(timeout));
  visibility_timeout_ = timeout;
}

void Worker::SetHeartbeatInterval(std::chrono::milliseconds interval) {
  ThrowIfInvalid(ValidatePollInterval(interval));
  heartbeat_interval_ = interval;
}

void Worker::Run() {
  running_.store(true);
  started_at_ms_ = NowUnixMs();
  last_heartbeat_at_ = {};
  spdlog::info("worker_start worker_id={} poll_interval_ms={} concurrency={}",
               worker_id_, poll_interval_.count(), thread_pool_->ThreadCount());
  MaybePublishHeartbeat();

  while (running_.load()) {
    MaybePublishHeartbeat();

    if (!CanAcceptTask()) {
      std::this_thread::sleep_for(poll_interval_);
      continue;
    }

    std::optional<TaskMessage> task;
    {
      std::lock_guard<std::mutex> lock(broker_mutex_);
      RunSchedulerTick(broker_, visibility_timeout_.count());
      task = broker_.Reserve();
    }

    if (!task.has_value()) {
      std::this_thread::sleep_for(poll_interval_);
      continue;
    }

    TaskMessage reserved_task = std::move(*task);
    thread_pool_->Submit([this, reserved_task = std::move(reserved_task)]() mutable {
      ProcessTask(reserved_task);
    });
  }

  thread_pool_->Shutdown();
  spdlog::info("worker_stop");
}

void Worker::Stop() { running_.store(false); }

bool Worker::RunOnce() {
  std::optional<TaskMessage> task;
  {
    std::lock_guard<std::mutex> lock(broker_mutex_);
    RunSchedulerTick(broker_, visibility_timeout_.count());
    task = broker_.Reserve();
  }

  if (!task.has_value()) {
    return false;
  }

  TaskMessage reserved_task = std::move(*task);
  thread_pool_->Submit([this, reserved_task = std::move(reserved_task)]() mutable {
    ProcessTask(reserved_task);
  });
  thread_pool_->WaitIdle();
  return true;
}

bool Worker::CanAcceptTask() const {
  return thread_pool_->InFlightCount() < thread_pool_->ThreadCount();
}

void Worker::MaybePublishHeartbeat() {
  const auto now = std::chrono::steady_clock::now();
  if (last_heartbeat_at_ != std::chrono::steady_clock::time_point{} &&
      now - last_heartbeat_at_ < heartbeat_interval_) {
    return;
  }
  last_heartbeat_at_ = now;

  WorkerHeartbeat heartbeat;
  heartbeat.worker_id = worker_id_;
  heartbeat.hostname = hostname_;
  heartbeat.pid = pid_;
  heartbeat.started_at_ms = started_at_ms_;
  heartbeat.last_seen_ms = NowUnixMs();
  heartbeat.concurrency = thread_pool_->ThreadCount();
  heartbeat.currently_running = thread_pool_->InFlightCount();

  std::lock_guard<std::mutex> lock(broker_mutex_);
  broker_.UpsertWorkerHeartbeat(heartbeat, heartbeat_ttl_seconds_);
}

void Worker::ProcessTask(const TaskMessage& task) {
  spdlog::info("task_start task_id={} task_name={}", task.id.Value(),
               task.name);

  const auto started_at = std::chrono::steady_clock::now();

  if (!registry_.HasTask(task.name)) {
    spdlog::error("task_unknown task_id={} task_name={}", task.id.Value(),
                  task.name);
    std::lock_guard<std::mutex> lock(broker_mutex_);
    broker_.Fail(task.id, "unknown task: " + task.name);
    return;
  }

  const TaskResult result = registry_.Execute(task.name, task.payload);
  const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started_at)
                               .count();

  std::lock_guard<std::mutex> lock(broker_mutex_);
  broker_.RecordDurationMs(metrics_names::kTaskExecutionDurationMs, duration_ms);

  if (result.success) {
    spdlog::info("task_complete task_id={} task_name={} success=true",
                 task.id.Value(), task.name);
    broker_.Ack(task.id);
    return;
  }

  spdlog::warn("task_complete task_id={} task_name={} success=false error={}",
               task.id.Value(), task.name, result.error_message);

  broker_.RecordCounter(metrics_names::kTasksFailedTotal);

  if (ShouldRetry(task)) {
    spdlog::info("task_retry task_id={} task_name={} retry_count={}",
                 task.id.Value(), task.name, task.retry_count + 1);
    broker_.Retry(task, result.error_message);
    return;
  }

  broker_.Fail(task.id, result.error_message);
}

}  // namespace tq

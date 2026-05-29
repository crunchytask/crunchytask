#include "taskqueue/worker.h"

#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

#include "taskqueue/clock.h"
#include "taskqueue/retry_policy.h"
#include "taskqueue/scheduler.h"

namespace tq {

Worker::Worker(Broker& broker, std::size_t concurrency)
    : broker_(broker), thread_pool_(std::make_unique<ThreadPool>(concurrency)) {}

void Worker::RegisterTask(std::string name, TaskHandler handler) {
  registry_.RegisterTask(std::move(name), std::move(handler));
}

void Worker::SetPollInterval(std::chrono::milliseconds interval) {
  poll_interval_ = interval;
}

void Worker::SetVisibilityTimeout(std::chrono::milliseconds timeout) {
  visibility_timeout_ = timeout;
}

void Worker::Run() {
  running_.store(true);
  spdlog::info("worker_start poll_interval_ms={} concurrency={}",
               poll_interval_.count(), thread_pool_->ThreadCount());

  while (running_.load()) {
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

void Worker::ProcessTask(const TaskMessage& task) {
  spdlog::info("task_start task_id={} task_name={}", task.id.Value(),
               task.name);

  if (!registry_.HasTask(task.name)) {
    spdlog::error("task_unknown task_id={} task_name={}", task.id.Value(),
                  task.name);
    std::lock_guard<std::mutex> lock(broker_mutex_);
    broker_.Fail(task.id, "unknown task: " + task.name);
    return;
  }

  const TaskResult result = registry_.Execute(task.name, task.payload);
  std::lock_guard<std::mutex> lock(broker_mutex_);
  if (result.success) {
    spdlog::info("task_complete task_id={} task_name={} success=true",
                 task.id.Value(), task.name);
    broker_.Ack(task.id);
    return;
  }

  spdlog::warn("task_complete task_id={} task_name={} success=false error={}",
               task.id.Value(), task.name, result.error_message);

  if (ShouldRetry(task)) {
    spdlog::info("task_retry task_id={} task_name={} retry_count={}",
                 task.id.Value(), task.name, task.retry_count + 1);
    broker_.Retry(task, result.error_message);
    return;
  }

  broker_.Fail(task.id, result.error_message);
}

}  // namespace tq

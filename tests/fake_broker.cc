#include "fake_broker.h"

#include <algorithm>

#include "taskqueue/clock.h"
#include "taskqueue/retry_policy.h"

namespace tq {
namespace testing {

namespace {

bool ShouldDelayUntil(const TaskMessage& task, std::int64_t now_ms) {
  return task.run_at_ms.has_value() && *task.run_at_ms > now_ms;
}

}  // namespace

void FakeBroker::AddDelayedTask(TaskMessage task, std::int64_t run_at_ms) {
  task.run_at_ms = run_at_ms;
  DelayedEntry entry{run_at_ms, std::move(task)};
  auto insert_at = std::lower_bound(
      delayed_.begin(), delayed_.end(), entry,
      [](const DelayedEntry& left, const DelayedEntry& right) {
        return left.run_at_ms < right.run_at_ms;
      });
  delayed_.insert(insert_at, std::move(entry));
}

TaskId FakeBroker::Enqueue(const TaskMessage& task) {
  const std::int64_t now_ms = NowUnixMs();
  if (ShouldDelayUntil(task, now_ms)) {
    TaskMessage message = task;
    message.status = TaskStatus::kPending;
    AddDelayedTask(std::move(message), *task.run_at_ms);
    statuses_[task.id.Value()] = TaskStatus::kPending;
    return task.id;
  }

  TaskMessage message = task;
  message.status = TaskStatus::kPending;
  message.run_at_ms.reset();
  pending_.push_back(std::move(message));
  statuses_[task.id.Value()] = TaskStatus::kPending;
  return task.id;
}

void FakeBroker::PromoteDueTasks(std::int64_t now_ms) {
  auto it = delayed_.begin();
  while (it != delayed_.end()) {
    if (it->run_at_ms > now_ms) {
      break;
    }

    TaskMessage message = it->task;
    message.status = TaskStatus::kPending;
    message.run_at_ms.reset();
    pending_.push_back(std::move(message));
    it = delayed_.erase(it);
  }
}

std::optional<TaskMessage> FakeBroker::Reserve() {
  if (pending_.empty()) {
    return std::nullopt;
  }

  TaskMessage message = pending_.front();
  pending_.pop_front();
  message.status = TaskStatus::kRunning;
  message.reserved_at_ms = NowUnixMs();
  running_[message.id.Value()] = message;
  statuses_[message.id.Value()] = TaskStatus::kRunning;
  return message;
}

int FakeBroker::ReclaimStaleTasks(std::int64_t now_ms,
                                  std::int64_t visibility_timeout_ms) {
  int reclaimed = 0;

  for (auto it = running_.begin(); it != running_.end();) {
    const TaskMessage& message = it->second;
    if (!message.reserved_at_ms.has_value()) {
      ++it;
      continue;
    }

    if (now_ms - *message.reserved_at_ms <= visibility_timeout_ms) {
      ++it;
      continue;
    }

    TaskMessage requeued = message;
    requeued.status = TaskStatus::kPending;
    requeued.reserved_at_ms.reset();
    pending_.push_back(std::move(requeued));
    statuses_[message.id.Value()] = TaskStatus::kPending;
    it = running_.erase(it);
    ++reclaimed;
  }

  return reclaimed;
}

void FakeBroker::Ack(const TaskId& id) {
  running_.erase(id.Value());
  statuses_[id.Value()] = TaskStatus::kSucceeded;
  failure_reasons_.erase(id.Value());
  results_[id.Value()] = TaskResult::Success();
}

void FakeBroker::Retry(const TaskMessage& task, const std::string& reason) {
  running_.erase(task.id.Value());

  TaskMessage retry_task = task;
  retry_task.retry_count += 1;
  retry_task.last_error = reason;
  retry_task.status = TaskStatus::kRetrying;
  retry_task.reserved_at_ms.reset();
  const std::int64_t run_at_ms =
      NowUnixMs() + ComputeRetryDelayMs(retry_task.retry_policy,
                                        retry_task.retry_count);
  AddDelayedTask(std::move(retry_task), run_at_ms);

  statuses_[task.id.Value()] = TaskStatus::kRetrying;
  failure_reasons_[task.id.Value()] = reason;
}

void FakeBroker::Fail(const TaskId& id, const std::string& reason) {
  TaskMessage message;
  const auto running = running_.find(id.Value());
  if (running != running_.end()) {
    message = running->second;
    running_.erase(running);
  } else {
    message.id = id;
  }

  message.status = TaskStatus::kDead;
  message.last_error = reason;
  dead_[id.Value()] = std::move(message);
  statuses_[id.Value()] = TaskStatus::kDead;
  failure_reasons_[id.Value()] = reason;
}

ParseResult<TaskStatus> FakeBroker::GetStatus(const TaskId& id) const {
  const auto status = statuses_.find(id.Value());
  if (status == statuses_.end()) {
    return ParseResult<TaskStatus>::Fail("task not found: " + id.Value());
  }
  return ParseResult<TaskStatus>::Ok(status->second);
}

std::vector<TaskMessage> FakeBroker::ListDeadTasks() const {
  std::vector<TaskMessage> tasks;
  tasks.reserve(dead_.size());
  for (const auto& entry : dead_) {
    tasks.push_back(entry.second);
  }
  return tasks;
}

ParseResult<std::string> FakeBroker::GetFailureReason(const TaskId& id) const {
  const auto reason = failure_reasons_.find(id.Value());
  if (reason == failure_reasons_.end()) {
    return ParseResult<std::string>::Fail("failure reason not found: " +
                                          id.Value());
  }
  return ParseResult<std::string>::Ok(reason->second);
}

ParseResult<TaskId> FakeBroker::RetryDeadTask(const TaskId& id) {
  const auto dead = dead_.find(id.Value());
  if (dead == dead_.end()) {
    return ParseResult<TaskId>::Fail("dead task not found: " + id.Value());
  }

  TaskMessage message = dead->second;
  dead_.erase(dead);
  message.status = TaskStatus::kPending;
  message.retry_count = 0;
  message.last_error.clear();
  message.run_at_ms.reset();
  message.reserved_at_ms.reset();
  pending_.push_back(std::move(message));
  statuses_[id.Value()] = TaskStatus::kPending;
  failure_reasons_.erase(id.Value());
  return ParseResult<TaskId>::Ok(id);
}

BrokerStats FakeBroker::GetStats() const {
  BrokerStats stats;
  stats.pending_count = pending_.size();
  stats.delayed_count = delayed_.size();
  stats.running_count = running_.size();
  stats.dead_count = dead_.size();
  return stats;
}

ParseResult<TaskResult> FakeBroker::GetTaskResult(const TaskId& id) const {
  const auto result = results_.find(id.Value());
  if (result == results_.end()) {
    return ParseResult<TaskResult>::Fail("task result not found: " + id.Value());
  }
  return ParseResult<TaskResult>::Ok(result->second);
}

}  // namespace testing
}  // namespace tq

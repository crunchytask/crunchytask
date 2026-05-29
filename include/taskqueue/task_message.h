#ifndef TASKQUEUE_TASK_MESSAGE_H_
#define TASKQUEUE_TASK_MESSAGE_H_

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "taskqueue/retry_policy.h"
#include "taskqueue/task_id.h"
#include "taskqueue/task_status.h"

namespace tq {

struct TaskMessage {
  TaskId id;
  std::string name;
  nlohmann::json payload = nlohmann::json::object();
  TaskStatus status = TaskStatus::kPending;
  int retry_count = 0;
  RetryPolicy retry_policy{};
  std::int64_t created_at_ms = 0;
  std::optional<std::int64_t> run_at_ms;
  std::optional<std::int64_t> reserved_at_ms;
  std::string last_error;

  static TaskMessage Create(std::string name, nlohmann::json payload);
  static TaskMessage CreateWithDelay(std::string name, nlohmann::json payload,
                                     std::int64_t delay_ms);

  bool operator==(const TaskMessage& other) const;
};

}  // namespace tq

#endif  // TASKQUEUE_TASK_MESSAGE_H_

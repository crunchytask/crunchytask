#include "taskqueue/task_message.h"

#include <chrono>

namespace tq {

TaskMessage TaskMessage::Create(std::string name, nlohmann::json payload) {
  TaskMessage message;
  message.id = TaskId::Generate();
  message.name = std::move(name);
  message.payload = std::move(payload);
  message.status = TaskStatus::kPending;
  message.retry_count = 0;
  message.retry_policy = RetryPolicy{};
  message.created_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
  message.last_error.clear();
  return message;
}

TaskMessage TaskMessage::CreateWithDelay(std::string name, nlohmann::json payload,
                                         std::int64_t delay_ms) {
  TaskMessage message = Create(std::move(name), std::move(payload));
  message.run_at_ms = message.created_at_ms + delay_ms;
  return message;
}

bool TaskMessage::operator==(const TaskMessage& other) const {
  return id == other.id && name == other.name && payload == other.payload &&
         status == other.status && retry_count == other.retry_count &&
         retry_policy == other.retry_policy &&
         created_at_ms == other.created_at_ms && run_at_ms == other.run_at_ms &&
         reserved_at_ms == other.reserved_at_ms &&
         last_error == other.last_error;
}

}  // namespace tq

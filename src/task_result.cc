#include "taskqueue/task_result.h"

namespace tq {

TaskResult TaskResult::Success(nlohmann::json payload) {
  TaskResult result;
  result.success = true;
  result.payload = std::move(payload);
  return result;
}

TaskResult TaskResult::Failure(std::string message) {
  TaskResult result;
  result.success = false;
  result.error_message = std::move(message);
  return result;
}

}  // namespace tq

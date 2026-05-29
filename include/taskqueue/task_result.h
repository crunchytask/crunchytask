#ifndef TASKQUEUE_TASK_RESULT_H_
#define TASKQUEUE_TASK_RESULT_H_

#include <string>

#include <nlohmann/json.hpp>

namespace tq {

struct TaskResult {
  bool success = false;
  nlohmann::json payload = nlohmann::json::object();
  std::string error_message;

  static TaskResult Success(nlohmann::json payload = nlohmann::json::object());
  static TaskResult Failure(std::string message);

  bool operator==(const TaskResult& other) const {
    return success == other.success && payload == other.payload &&
           error_message == other.error_message;
  }
};

}  // namespace tq

#endif  // TASKQUEUE_TASK_RESULT_H_

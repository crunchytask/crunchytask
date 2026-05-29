#include "taskqueue/task_status.h"

#include <string>
#include <string_view>

namespace tq {

const char* TaskStatusToString(TaskStatus status) {
  switch (status) {
    case TaskStatus::kPending:
      return "pending";
    case TaskStatus::kRunning:
      return "running";
    case TaskStatus::kSucceeded:
      return "succeeded";
    case TaskStatus::kFailed:
      return "failed";
    case TaskStatus::kRetrying:
      return "retrying";
    case TaskStatus::kDead:
      return "dead";
  }
  return "pending";
}

ParseResult<TaskStatus> TaskStatusFromString(std::string_view value) {
  if (value == "pending") {
    return ParseResult<TaskStatus>::Ok(TaskStatus::kPending);
  }
  if (value == "running") {
    return ParseResult<TaskStatus>::Ok(TaskStatus::kRunning);
  }
  if (value == "succeeded") {
    return ParseResult<TaskStatus>::Ok(TaskStatus::kSucceeded);
  }
  if (value == "failed") {
    return ParseResult<TaskStatus>::Ok(TaskStatus::kFailed);
  }
  if (value == "retrying") {
    return ParseResult<TaskStatus>::Ok(TaskStatus::kRetrying);
  }
  if (value == "dead") {
    return ParseResult<TaskStatus>::Ok(TaskStatus::kDead);
  }
  return ParseResult<TaskStatus>::Fail("unknown task status: " +
                                        std::string(value));
}

}  // namespace tq

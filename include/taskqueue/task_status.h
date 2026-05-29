#ifndef TASKQUEUE_TASK_STATUS_H_
#define TASKQUEUE_TASK_STATUS_H_

#include <string_view>

#include "taskqueue/parse_result.h"

namespace tq {

enum class TaskStatus {
  kPending,
  kRunning,
  kSucceeded,
  kFailed,
  kRetrying,
  kDead,
};

const char* TaskStatusToString(TaskStatus status);
ParseResult<TaskStatus> TaskStatusFromString(std::string_view value);

}  // namespace tq

#endif  // TASKQUEUE_TASK_STATUS_H_

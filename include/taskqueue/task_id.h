#ifndef TASKQUEUE_TASK_ID_H_
#define TASKQUEUE_TASK_ID_H_

#include <string>
#include <string_view>

#include "taskqueue/parse_result.h"

namespace tq {

class TaskId {
 public:
  TaskId();

  static TaskId Generate();
  static ParseResult<TaskId> Parse(std::string_view value);

  const std::string& Value() const { return value_; }

  bool operator==(const TaskId& other) const { return value_ == other.value_; }
  bool operator!=(const TaskId& other) const { return !(*this == other); }

 private:
  explicit TaskId(std::string value);

  std::string value_;
};

}  // namespace tq

#endif  // TASKQUEUE_TASK_ID_H_

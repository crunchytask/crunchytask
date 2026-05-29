#ifndef TASKQUEUE_TASK_HANDLER_H_
#define TASKQUEUE_TASK_HANDLER_H_

#include <functional>

#include <nlohmann/json.hpp>

#include "taskqueue/task_result.h"

namespace tq {

using TaskHandler = std::function<TaskResult(const nlohmann::json& payload)>;

}  // namespace tq

#endif  // TASKQUEUE_TASK_HANDLER_H_

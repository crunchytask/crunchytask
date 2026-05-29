#ifndef TASKQUEUE_TASK_REGISTRY_H_
#define TASKQUEUE_TASK_REGISTRY_H_

#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "taskqueue/task_handler.h"
#include "taskqueue/task_result.h"

namespace tq {

class TaskRegistry {
 public:
  void RegisterTask(std::string name, TaskHandler handler);
  bool HasTask(const std::string& name) const;
  TaskResult Execute(const std::string& name,
                     const nlohmann::json& payload) const;

 private:
  std::unordered_map<std::string, TaskHandler> handlers_;
};

}  // namespace tq

#endif  // TASKQUEUE_TASK_REGISTRY_H_

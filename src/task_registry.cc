#include "taskqueue/task_registry.h"

namespace tq {

void TaskRegistry::RegisterTask(std::string name, TaskHandler handler) {
  handlers_[std::move(name)] = std::move(handler);
}

bool TaskRegistry::HasTask(const std::string& name) const {
  return handlers_.find(name) != handlers_.end();
}

TaskResult TaskRegistry::Execute(const std::string& name,
                                 const nlohmann::json& payload) const {
  const auto handler = handlers_.find(name);
  if (handler == handlers_.end()) {
    return TaskResult::Failure("unknown task: " + name);
  }
  return handler->second(payload);
}

}  // namespace tq

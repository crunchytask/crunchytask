#ifndef TASKQUEUE_TASK_JSON_H_
#define TASKQUEUE_TASK_JSON_H_

#include <string>

#include <nlohmann/json.hpp>

#include "taskqueue/parse_result.h"
#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"

namespace tq {

nlohmann::json ToJson(const TaskMessage& message);
ParseResult<TaskMessage> TaskMessageFromJson(const nlohmann::json& json);
ParseResult<TaskMessage> TaskMessageFromJsonString(const std::string& json_text);

nlohmann::json ToJson(const TaskResult& result);
ParseResult<TaskResult> TaskResultFromJson(const nlohmann::json& json);

nlohmann::json ToJson(const RetryPolicy& policy);
ParseResult<RetryPolicy> RetryPolicyFromJson(const nlohmann::json& json);

}  // namespace tq

#endif  // TASKQUEUE_TASK_JSON_H_

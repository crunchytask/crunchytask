#include "taskqueue/task_json.h"

#include <string>

namespace tq {

namespace {

template <typename T>
ParseResult<T> RequireField(const nlohmann::json& json, const char* key) {
  if (!json.contains(key)) {
    return ParseResult<T>::Fail(std::string("missing required field: ") + key);
  }
  return ParseResult<T>::Ok(T{});
}

ParseResult<std::int64_t> ParseInt64Field(const nlohmann::json& json,
                                          const char* key) {
  auto check = RequireField<nlohmann::json>(json, key);
  if (!check.Ok()) {
    return ParseResult<std::int64_t>::Fail(check.Error());
  }
  if (!json.at(key).is_number_integer()) {
    return ParseResult<std::int64_t>::Fail(std::string("field must be int: ") +
                                           key);
  }
  return ParseResult<std::int64_t>::Ok(json.at(key).get<std::int64_t>());
}

ParseResult<int> ParseIntField(const nlohmann::json& json, const char* key) {
  auto check = RequireField<nlohmann::json>(json, key);
  if (!check.Ok()) {
    return ParseResult<int>::Fail(check.Error());
  }
  if (!json.at(key).is_number_integer()) {
    return ParseResult<int>::Fail(std::string("field must be int: ") + key);
  }
  return ParseResult<int>::Ok(json.at(key).get<int>());
}

ParseResult<double> ParseDoubleField(const nlohmann::json& json,
                                     const char* key) {
  auto check = RequireField<nlohmann::json>(json, key);
  if (!check.Ok()) {
    return ParseResult<double>::Fail(check.Error());
  }
  if (!json.at(key).is_number()) {
    return ParseResult<double>::Fail(std::string("field must be number: ") +
                                     key);
  }
  return ParseResult<double>::Ok(json.at(key).get<double>());
}

ParseResult<std::string> ParseStringField(const nlohmann::json& json,
                                          const char* key) {
  auto check = RequireField<nlohmann::json>(json, key);
  if (!check.Ok()) {
    return ParseResult<std::string>::Fail(check.Error());
  }
  if (!json.at(key).is_string()) {
    return ParseResult<std::string>::Fail(std::string("field must be string: ") +
                                          key);
  }
  return ParseResult<std::string>::Ok(json.at(key).get<std::string>());
}

}  // namespace

nlohmann::json ToJson(const RetryPolicy& policy) {
  return nlohmann::json{{"max_retries", policy.max_retries},
                        {"base_delay_ms", policy.base_delay_ms},
                        {"multiplier", policy.multiplier}};
}

ParseResult<RetryPolicy> RetryPolicyFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    return ParseResult<RetryPolicy>::Fail("retry_policy must be an object");
  }

  RetryPolicy policy;

  auto max_retries = ParseIntField(json, "max_retries");
  if (!max_retries.Ok()) {
    return ParseResult<RetryPolicy>::Fail(max_retries.Error());
  }
  policy.max_retries = max_retries.Value();

  auto base_delay_ms = ParseInt64Field(json, "base_delay_ms");
  if (!base_delay_ms.Ok()) {
    return ParseResult<RetryPolicy>::Fail(base_delay_ms.Error());
  }
  policy.base_delay_ms = base_delay_ms.Value();

  auto multiplier = ParseDoubleField(json, "multiplier");
  if (!multiplier.Ok()) {
    return ParseResult<RetryPolicy>::Fail(multiplier.Error());
  }
  policy.multiplier = multiplier.Value();

  return ParseResult<RetryPolicy>::Ok(policy);
}

nlohmann::json ToJson(const TaskResult& result) {
  nlohmann::json json{
      {"success", result.success},
      {"payload", result.payload},
      {"error_message", result.error_message},
  };
  return json;
}

ParseResult<TaskResult> TaskResultFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    return ParseResult<TaskResult>::Fail("task result must be an object");
  }

  TaskResult result;

  auto success_check = RequireField<bool>(json, "success");
  if (!success_check.Ok()) {
    return ParseResult<TaskResult>::Fail(success_check.Error());
  }
  if (!json.at("success").is_boolean()) {
    return ParseResult<TaskResult>::Fail("success must be a boolean");
  }
  result.success = json.at("success").get<bool>();

  if (!json.contains("payload")) {
    return ParseResult<TaskResult>::Fail("missing required field: payload");
  }
  result.payload = json.at("payload");

  auto error_message = ParseStringField(json, "error_message");
  if (!error_message.Ok()) {
    return ParseResult<TaskResult>::Fail(error_message.Error());
  }
  result.error_message = error_message.Value();

  return ParseResult<TaskResult>::Ok(result);
}

nlohmann::json ToJson(const TaskMessage& message) {
  nlohmann::json json{
      {"id", message.id.Value()},
      {"name", message.name},
      {"payload", message.payload},
      {"status", TaskStatusToString(message.status)},
      {"retry_count", message.retry_count},
      {"retry_policy", ToJson(message.retry_policy)},
      {"created_at_ms", message.created_at_ms},
  };

  if (message.run_at_ms.has_value()) {
    json["run_at_ms"] = *message.run_at_ms;
  }

  if (!message.last_error.empty()) {
    json["last_error"] = message.last_error;
  }

  if (message.reserved_at_ms.has_value()) {
    json["reserved_at_ms"] = *message.reserved_at_ms;
  }

  return json;
}

ParseResult<TaskMessage> TaskMessageFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    return ParseResult<TaskMessage>::Fail("task message must be an object");
  }

  TaskMessage message;

  auto id_string = ParseStringField(json, "id");
  if (!id_string.Ok()) {
    return ParseResult<TaskMessage>::Fail(id_string.Error());
  }
  auto id = TaskId::Parse(id_string.Value());
  if (!id.Ok()) {
    return ParseResult<TaskMessage>::Fail(id.Error());
  }
  message.id = id.Value();

  auto name = ParseStringField(json, "name");
  if (!name.Ok()) {
    return ParseResult<TaskMessage>::Fail(name.Error());
  }
  message.name = name.Value();

  if (!json.contains("payload")) {
    return ParseResult<TaskMessage>::Fail("missing required field: payload");
  }
  message.payload = json.at("payload");

  auto status_string = ParseStringField(json, "status");
  if (!status_string.Ok()) {
    return ParseResult<TaskMessage>::Fail(status_string.Error());
  }
  auto status = TaskStatusFromString(status_string.Value());
  if (!status.Ok()) {
    return ParseResult<TaskMessage>::Fail(status.Error());
  }
  message.status = status.Value();

  auto retry_count = ParseIntField(json, "retry_count");
  if (!retry_count.Ok()) {
    return ParseResult<TaskMessage>::Fail(retry_count.Error());
  }
  message.retry_count = retry_count.Value();

  if (!json.contains("retry_policy")) {
    return ParseResult<TaskMessage>::Fail("missing required field: retry_policy");
  }
  auto retry_policy = RetryPolicyFromJson(json.at("retry_policy"));
  if (!retry_policy.Ok()) {
    return ParseResult<TaskMessage>::Fail(retry_policy.Error());
  }
  message.retry_policy = retry_policy.Value();

  auto created_at_ms = ParseInt64Field(json, "created_at_ms");
  if (!created_at_ms.Ok()) {
    return ParseResult<TaskMessage>::Fail(created_at_ms.Error());
  }
  message.created_at_ms = created_at_ms.Value();

  if (json.contains("run_at_ms")) {
    if (!json.at("run_at_ms").is_number_integer()) {
      return ParseResult<TaskMessage>::Fail("run_at_ms must be an integer");
    }
    message.run_at_ms = json.at("run_at_ms").get<std::int64_t>();
  }

  if (json.contains("last_error")) {
    auto last_error = ParseStringField(json, "last_error");
    if (!last_error.Ok()) {
      return ParseResult<TaskMessage>::Fail(last_error.Error());
    }
    message.last_error = last_error.Value();
  }

  if (json.contains("reserved_at_ms")) {
    if (!json.at("reserved_at_ms").is_number_integer()) {
      return ParseResult<TaskMessage>::Fail("reserved_at_ms must be an integer");
    }
    message.reserved_at_ms = json.at("reserved_at_ms").get<std::int64_t>();
  }

  return ParseResult<TaskMessage>::Ok(message);
}

ParseResult<TaskMessage> TaskMessageFromJsonString(const std::string& json_text) {
  try {
    return TaskMessageFromJson(nlohmann::json::parse(json_text));
  } catch (const nlohmann::json::parse_error& error) {
    return ParseResult<TaskMessage>::Fail(std::string("invalid json: ") +
                                           error.what());
  }
}

}  // namespace tq

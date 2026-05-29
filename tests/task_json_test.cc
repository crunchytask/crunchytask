#include "taskqueue/task_json.h"
#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

tq::TaskMessage MakeSampleMessage() {
  tq::TaskMessage message;
  auto id = tq::TaskId::Parse("550e8400-e29b-41d4-a716-446655440000");
  REQUIRE(id.Ok());
  message.id = id.Value();
  message.name = "add";
  message.payload = nlohmann::json{{"a", 2}, {"b", 3}};
  message.status = tq::TaskStatus::kPending;
  message.retry_count = 0;
  message.retry_policy = tq::RetryPolicy{};
  message.created_at_ms = 1717000000000;
  message.run_at_ms = 1717000001000;
  return message;
}

}  // namespace

TEST_CASE("TaskMessage round-trips through JSON", "[task_json]") {
  const tq::TaskMessage original = MakeSampleMessage();

  const nlohmann::json json = tq::ToJson(original);
  const auto parsed = tq::TaskMessageFromJson(json);

  REQUIRE(parsed.Ok());
  REQUIRE(parsed.Value() == original);
}

TEST_CASE("TaskMessage Create assigns a non-empty id", "[unit][task_message]") {
  const tq::TaskMessage message =
      tq::TaskMessage::Create("add", nlohmann::json{{"a", 1}});

  REQUIRE_FALSE(message.id.Value().empty());
  REQUIRE(message.name == "add");
  REQUIRE(message.status == tq::TaskStatus::kPending);
  REQUIRE(message.retry_count == 0);
  REQUIRE(message.retry_policy.max_retries == 3);
  REQUIRE(message.created_at_ms > 0);
}

TEST_CASE("TaskMessage CreateWithDelay sets run_at_ms", "[unit][task_message]") {
  const tq::TaskMessage message =
      tq::TaskMessage::CreateWithDelay("add", nlohmann::json{{"a", 1}}, 250);

  REQUIRE(message.run_at_ms.has_value());
  REQUIRE(*message.run_at_ms == message.created_at_ms + 250);
}

TEST_CASE("TaskMessage serializes reserved_at_ms and last_error", "[unit][task_json]") {
  tq::TaskMessage message = MakeSampleMessage();
  message.last_error = "transient";
  message.reserved_at_ms = 1717000002000;

  const auto parsed = tq::TaskMessageFromJson(tq::ToJson(message));
  REQUIRE(parsed.Ok());
  REQUIRE(parsed.Value().last_error == "transient");
  REQUIRE(parsed.Value().reserved_at_ms == message.reserved_at_ms);
}

TEST_CASE("TaskMessageFromJsonString rejects malformed JSON", "[unit][task_json]") {
  const auto parsed = tq::TaskMessageFromJsonString("{not json");

  REQUIRE_FALSE(parsed.Ok());
  REQUIRE(parsed.Error().find("invalid json") != std::string::npos);
}

TEST_CASE("TaskMessageFromJson rejects missing name", "[task_json]") {
  const nlohmann::json json = {
      {"id", "550e8400-e29b-41d4-a716-446655440000"},
      {"payload", nlohmann::json::object()},
      {"status", "pending"},
      {"retry_count", 0},
      {"retry_policy",
       {{"max_retries", 3}, {"base_delay_ms", 1000}, {"multiplier", 2.0}}},
      {"created_at_ms", 1717000000000},
  };

  const auto parsed = tq::TaskMessageFromJson(json);

  REQUIRE_FALSE(parsed.Ok());
  REQUIRE(parsed.Error() == "missing required field: name");
}

TEST_CASE("TaskMessageFromJson rejects unknown status", "[task_json]") {
  nlohmann::json json = tq::ToJson(MakeSampleMessage());
  json["status"] = "unknown";

  const auto parsed = tq::TaskMessageFromJson(json);

  REQUIRE_FALSE(parsed.Ok());
  REQUIRE(parsed.Error().find("unknown task status") != std::string::npos);
}

TEST_CASE("RetryPolicy defaults are sensible", "[retry_policy]") {
  const tq::RetryPolicy policy;

  REQUIRE(policy.max_retries == 3);
  REQUIRE(policy.base_delay_ms == 1000);
  REQUIRE(policy.multiplier == 2.0);
}

TEST_CASE("TaskResult serializes success and failure", "[task_result]") {
  const tq::TaskResult success =
      tq::TaskResult::Success(nlohmann::json{{"result", 5}});
  const auto success_parsed = tq::TaskResultFromJson(tq::ToJson(success));
  REQUIRE(success_parsed.Ok());
  REQUIRE(success_parsed.Value() == success);

  const tq::TaskResult failure = tq::TaskResult::Failure("boom");
  const auto failure_parsed = tq::TaskResultFromJson(tq::ToJson(failure));
  REQUIRE(failure_parsed.Ok());
  REQUIRE(failure_parsed.Value() == failure);
}

TEST_CASE("TaskId Parse validates UUID format", "[task_id]") {
  const auto valid = tq::TaskId::Parse("550e8400-e29b-41d4-a716-446655440000");
  REQUIRE(valid.Ok());

  const auto invalid = tq::TaskId::Parse("not-a-uuid");
  REQUIRE_FALSE(invalid.Ok());
}

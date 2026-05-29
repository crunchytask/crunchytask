#include "fake_broker.h"

#include "taskqueue/runtime_config.h"
#include "taskqueue/task_json.h"
#include "taskqueue/task_message.h"
#include "taskqueue/worker.h"

#include <chrono>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ValidateRedisUri accepts non-empty uri", "[unit][config]") {
  const auto result = tq::ValidateRedisUri("tcp://127.0.0.1:6379");
  REQUIRE(result.Ok());
}

TEST_CASE("ValidateRedisUri rejects empty uri", "[unit][config]") {
  const auto result = tq::ValidateRedisUri("");
  REQUIRE_FALSE(result.Ok());
  REQUIRE(result.Error() == "redis uri must not be empty");
}

TEST_CASE("ValidateWorkerConcurrency accepts positive values", "[unit][config]") {
  REQUIRE(tq::ValidateWorkerConcurrency(1).Ok());
  REQUIRE(tq::ValidateWorkerConcurrency(4).Ok());
}

TEST_CASE("ValidateWorkerConcurrency rejects zero", "[unit][config]") {
  const auto result = tq::ValidateWorkerConcurrency(0);
  REQUIRE_FALSE(result.Ok());
  REQUIRE(result.Error() == "worker concurrency must be >= 1");
}

TEST_CASE("ValidateVisibilityTimeout accepts positive values", "[unit][config]") {
  REQUIRE(tq::ValidateVisibilityTimeout(std::chrono::milliseconds(1)).Ok());
  REQUIRE(tq::ValidateVisibilityTimeout(std::chrono::milliseconds(30000)).Ok());
}

TEST_CASE("ValidateVisibilityTimeout rejects non-positive values", "[unit][config]") {
  const auto result = tq::ValidateVisibilityTimeout(std::chrono::milliseconds(0));
  REQUIRE_FALSE(result.Ok());
  REQUIRE(result.Error() == "visibility timeout must be > 0");
}

TEST_CASE("ValidatePollInterval accepts positive values", "[unit][config]") {
  REQUIRE(tq::ValidatePollInterval(std::chrono::milliseconds(100)).Ok());
}

TEST_CASE("ValidatePollInterval rejects non-positive values", "[unit][config]") {
  const auto result = tq::ValidatePollInterval(std::chrono::milliseconds(0));
  REQUIRE_FALSE(result.Ok());
  REQUIRE(result.Error() == "poll interval must be > 0");
}

TEST_CASE("ValidateRetryPolicy accepts defaults", "[unit][config]") {
  REQUIRE(tq::ValidateRetryPolicy(tq::RetryPolicy{}).Ok());
}

TEST_CASE("ValidateRetryPolicy rejects negative max_retries", "[unit][config]") {
  tq::RetryPolicy policy;
  policy.max_retries = -1;
  const auto result = tq::ValidateRetryPolicy(policy);
  REQUIRE_FALSE(result.Ok());
  REQUIRE(result.Error() == "retry max_retries must be >= 0");
}

TEST_CASE("ValidateRetryPolicy rejects negative base_delay_ms", "[unit][config]") {
  tq::RetryPolicy policy;
  policy.base_delay_ms = -1;
  const auto result = tq::ValidateRetryPolicy(policy);
  REQUIRE_FALSE(result.Ok());
  REQUIRE(result.Error() == "retry base_delay_ms must be >= 0");
}

TEST_CASE("ValidateRetryPolicy rejects negative multiplier", "[unit][config]") {
  tq::RetryPolicy policy;
  policy.multiplier = -0.5;
  const auto result = tq::ValidateRetryPolicy(policy);
  REQUIRE_FALSE(result.Ok());
  REQUIRE(result.Error() == "retry multiplier must be >= 0");
}

TEST_CASE("ValidateDelayMs accepts zero and positive values", "[unit][config]") {
  REQUIRE(tq::ValidateDelayMs(0).Ok());
  REQUIRE(tq::ValidateDelayMs(5000).Ok());
}

TEST_CASE("ValidateDelayMs rejects negative values", "[unit][config]") {
  const auto result = tq::ValidateDelayMs(-1);
  REQUIRE_FALSE(result.Ok());
  REQUIRE(result.Error() == "delay_ms must be >= 0");
}

TEST_CASE("RetryPolicyFromJson rejects invalid retry policy", "[unit][config]") {
  const auto result = tq::RetryPolicyFromJson(
      nlohmann::json{{"max_retries", -1}, {"base_delay_ms", 1000}, {"multiplier", 2.0}});
  REQUIRE_FALSE(result.Ok());
  REQUIRE(result.Error() == "retry max_retries must be >= 0");
}

TEST_CASE("Worker rejects invalid concurrency", "[unit][config]") {
  tq::testing::FakeBroker broker;
  REQUIRE_THROWS_AS((tq::Worker(broker, 0)), std::invalid_argument);
}

TEST_CASE("Worker rejects invalid poll interval", "[unit][config]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);
  REQUIRE_THROWS_AS(worker.SetPollInterval(std::chrono::milliseconds(0)),
                    std::invalid_argument);
}

TEST_CASE("Worker rejects invalid visibility timeout", "[unit][config]") {
  tq::testing::FakeBroker broker;
  tq::Worker worker(broker);
  REQUIRE_THROWS_AS(worker.SetVisibilityTimeout(std::chrono::milliseconds(0)),
                    std::invalid_argument);
}

TEST_CASE("TaskMessage CreateWithDelay rejects negative delay", "[unit][config]") {
  REQUIRE_THROWS_AS(
      tq::TaskMessage::CreateWithDelay("add", nlohmann::json{{"a", 1}}, -1),
      std::invalid_argument);
}

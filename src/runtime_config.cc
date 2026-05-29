#include "taskqueue/runtime_config.h"

#include <cstdlib>

namespace tq {

namespace {

ConfigValidation Ok() { return ConfigValidation::Ok(ConfigValid{}); }

}  // namespace

ConfigValidation ValidateRedisUri(const std::string& redis_uri) {
  if (redis_uri.empty()) {
    return ConfigValidation::Fail("redis uri must not be empty");
  }
  return Ok();
}

ConfigValidation ValidateWorkerConcurrency(const std::size_t concurrency) {
  if (concurrency < 1) {
    return ConfigValidation::Fail("worker concurrency must be >= 1");
  }
  return Ok();
}

ConfigValidation ValidateVisibilityTimeout(
    const std::chrono::milliseconds timeout) {
  if (timeout.count() <= 0) {
    return ConfigValidation::Fail("visibility timeout must be > 0");
  }
  return Ok();
}

ConfigValidation ValidatePollInterval(const std::chrono::milliseconds interval) {
  if (interval.count() <= 0) {
    return ConfigValidation::Fail("poll interval must be > 0");
  }
  return Ok();
}

ConfigValidation ValidateRetryPolicy(const RetryPolicy& policy) {
  if (policy.max_retries < 0) {
    return ConfigValidation::Fail("retry max_retries must be >= 0");
  }
  if (policy.base_delay_ms < 0) {
    return ConfigValidation::Fail("retry base_delay_ms must be >= 0");
  }
  if (policy.multiplier < 0.0) {
    return ConfigValidation::Fail("retry multiplier must be >= 0");
  }
  return Ok();
}

ConfigValidation ValidateDelayMs(const std::int64_t delay_ms) {
  if (delay_ms < 0) {
    return ConfigValidation::Fail("delay_ms must be >= 0");
  }
  return Ok();
}

std::string DefaultRedisUriFromEnv() {
  const char* uri = std::getenv("TASKQUEUE_REDIS_URI");
  if (uri != nullptr && uri[0] != '\0') {
    return uri;
  }
  return "tcp://127.0.0.1:6379";
}

}  // namespace tq

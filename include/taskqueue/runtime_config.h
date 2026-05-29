#ifndef TASKQUEUE_RUNTIME_CONFIG_H_
#define TASKQUEUE_RUNTIME_CONFIG_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "taskqueue/parse_result.h"
#include "taskqueue/retry_policy.h"

namespace tq {

struct ConfigValid {};

using ConfigValidation = ParseResult<ConfigValid>;

ConfigValidation ValidateRedisUri(const std::string& redis_uri);
ConfigValidation ValidateWorkerConcurrency(std::size_t concurrency);
ConfigValidation ValidateVisibilityTimeout(std::chrono::milliseconds timeout);
ConfigValidation ValidatePollInterval(std::chrono::milliseconds interval);
ConfigValidation ValidateRetryPolicy(const RetryPolicy& policy);
ConfigValidation ValidateDelayMs(std::int64_t delay_ms);

std::string DefaultRedisUriFromEnv();

}  // namespace tq

#endif  // TASKQUEUE_RUNTIME_CONFIG_H_

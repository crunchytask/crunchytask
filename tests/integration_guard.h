#ifndef TASKQUEUE_TESTS_INTEGRATION_GUARD_H_
#define TASKQUEUE_TESTS_INTEGRATION_GUARD_H_

#include <cstdlib>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "taskqueue/redis_broker.h"

namespace tq {
namespace testing {

inline std::string RedisUri() {
  const char* uri = std::getenv("TASKQUEUE_REDIS_URI");
  if (uri != nullptr && uri[0] != '\0') {
    return uri;
  }
  return "tcp://127.0.0.1:6379";
}

inline bool SkipIfNoRedis(const std::string& redis_uri = RedisUri()) {
  if (tq::RedisBroker::Ping(redis_uri)) {
    return false;
  }

  SUCCEED("Skipped: Redis not available at " << redis_uri);
  return true;
}

}  // namespace testing
}  // namespace tq

#endif  // TASKQUEUE_TESTS_INTEGRATION_GUARD_H_

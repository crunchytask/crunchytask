#include <iostream>
#include <string>

#include "taskqueue/runtime_config.h"
#include "taskqueue/task_message.h"
#include "taskqueue/version.h"

#ifdef TASKQUEUE_HAS_REDIS
#include "taskqueue/redis_broker.h"
#endif

namespace {

}  // namespace

int main() {
  std::cout << "taskqueue producer example\n";
  std::cout << "version: " << tq::kVersionString << '\n';

#ifdef TASKQUEUE_HAS_REDIS
  const std::string redis_uri = tq::DefaultRedisUriFromEnv();
  if (!tq::RedisBroker::Ping(redis_uri)) {
    std::cerr << "Redis not available at " << redis_uri << '\n';
    std::cerr << "Start Redis with: docker compose up -d redis\n";
    return 1;
  }

  tq::RedisBroker broker(redis_uri);
  const tq::TaskMessage immediate =
      tq::TaskMessage::Create("add", nlohmann::json{{"a", 2}, {"b", 3}});
  const tq::TaskId immediate_id = broker.Enqueue(immediate);

  const tq::TaskMessage delayed = tq::TaskMessage::CreateWithDelay(
      "add", nlohmann::json{{"a", 10}, {"b", 1}}, 5000);
  const tq::TaskId delayed_id = broker.Enqueue(delayed);

  std::cout << "enqueued immediate task id: " << immediate_id.Value() << '\n';
  std::cout << "enqueued delayed task id: " << delayed_id.Value()
            << " run_at_ms=" << *delayed.run_at_ms << '\n';

  const auto status = broker.GetStatus(immediate_id);
  if (status.Ok()) {
    std::cout << "status: " << tq::TaskStatusToString(status.Value()) << '\n';
  }
#else
  std::cout << "Redis support disabled at build time\n";
#endif

  return 0;
}

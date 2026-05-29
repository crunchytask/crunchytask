#ifndef TASKQUEUE_WORKER_HEARTBEAT_H_
#define TASKQUEUE_WORKER_HEARTBEAT_H_

#include <cstdint>
#include <string>

namespace tq {

struct WorkerHeartbeat {
  std::string worker_id;
  std::string hostname;
  std::int64_t pid = 0;
  std::int64_t started_at_ms = 0;
  std::int64_t last_seen_ms = 0;
  std::size_t concurrency = 0;
  std::size_t currently_running = 0;

  bool operator==(const WorkerHeartbeat& other) const;
};

inline constexpr std::int64_t kDefaultWorkerHeartbeatIntervalMs = 5000;
inline constexpr std::int64_t kDefaultWorkerHeartbeatTtlSeconds = 15;

std::string ReadHostname();

}  // namespace tq

#endif  // TASKQUEUE_WORKER_HEARTBEAT_H_

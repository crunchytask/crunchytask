#include "taskqueue/worker_heartbeat.h"

#include <unistd.h>

namespace tq {

bool WorkerHeartbeat::operator==(const WorkerHeartbeat& other) const {
  return worker_id == other.worker_id && hostname == other.hostname &&
         pid == other.pid && started_at_ms == other.started_at_ms &&
         last_seen_ms == other.last_seen_ms &&
         concurrency == other.concurrency &&
         currently_running == other.currently_running;
}

std::string ReadHostname() {
  char buffer[256] = {};
  if (gethostname(buffer, sizeof(buffer) - 1) != 0) {
    return "unknown";
  }
  return std::string(buffer);
}

}  // namespace tq

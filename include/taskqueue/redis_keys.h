#ifndef TASKQUEUE_REDIS_KEYS_H_
#define TASKQUEUE_REDIS_KEYS_H_

namespace tq {
namespace redis_keys {

inline constexpr const char kPending[] = "taskq:pending";
inline constexpr const char kDelayed[] = "taskq:delayed";
inline constexpr const char kRunning[] = "taskq:running";
inline constexpr const char kStatus[] = "taskq:status";
inline constexpr const char kResults[] = "taskq:results";
inline constexpr const char kDead[] = "taskq:dead";
inline constexpr const char kFailures[] = "taskq:failures";

}  // namespace redis_keys
}  // namespace tq

#endif  // TASKQUEUE_REDIS_KEYS_H_

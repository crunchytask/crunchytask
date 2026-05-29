#include "taskqueue/redis_broker.h"

#include <iterator>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sw/redis++/redis++.h>

#include "taskqueue/clock.h"
#include "taskqueue/metrics.h"
#include "taskqueue/metrics_snapshot.h"
#include "taskqueue/redis_keys.h"
#include "taskqueue/retry_policy.h"
#include "taskqueue/task_json.h"
#include "taskqueue/task_result.h"
#include "taskqueue/task_status.h"

namespace tq {

namespace {

constexpr const char kCounterPrefix[] = "c:";
constexpr const char kHistogramPrefix[] = "h:";

std::string CounterField(const std::string& name) {
  return std::string(kCounterPrefix) + name;
}

std::string HistogramCountField(const std::string& name) {
  return std::string(kHistogramPrefix) + name + ":count";
}

std::string HistogramSumField(const std::string& name) {
  return std::string(kHistogramPrefix) + name + ":sum";
}

std::string HistogramMaxField(const std::string& name) {
  return std::string(kHistogramPrefix) + name + ":max";
}

bool StartsWith(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

}  // namespace

RedisBroker::RedisBroker(std::string redis_uri)
    : redis_(std::make_unique<sw::redis::Redis>(std::move(redis_uri))) {}

RedisBroker::~RedisBroker() = default;

bool RedisBroker::Ping(const std::string& redis_uri) {
  try {
    sw::redis::Redis redis(redis_uri);
    return redis.ping() == "PONG";
  } catch (const sw::redis::Error&) {
    return false;
  }
}

TaskId RedisBroker::Enqueue(const TaskMessage& task) {
  TaskMessage message = task;
  if (message.id.Value().empty()) {
    message.id = TaskId::Generate();
  }
  message.status = TaskStatus::kPending;

  const std::int64_t now_ms = NowUnixMs();
  const std::string json = ToJson(message).dump();
  if (message.run_at_ms.has_value() && *message.run_at_ms > now_ms) {
    redis_->zadd(redis_keys::kDelayed, json,
                 static_cast<double>(*message.run_at_ms));
    redis_->hset(redis_keys::kStatus, message.id.Value(),
                 TaskStatusToString(TaskStatus::kPending));
    RecordCounter(metrics_names::kTasksEnqueuedTotal);
    return message.id;
  }

  message.run_at_ms.reset();
  redis_->lpush(redis_keys::kPending, ToJson(message).dump());
  redis_->hset(redis_keys::kStatus, message.id.Value(),
               TaskStatusToString(TaskStatus::kPending));
  RecordCounter(metrics_names::kTasksEnqueuedTotal);
  return message.id;
}

void RedisBroker::PromoteDueTasks(std::int64_t now_ms) {
  std::vector<std::string> due_tasks;
  redis_->zrangebyscore(
      redis_keys::kDelayed,
      sw::redis::RightBoundedInterval<double>(static_cast<double>(now_ms),
                                              sw::redis::BoundType::LEFT_OPEN),
      std::back_inserter(due_tasks));

  for (const auto& json_text : due_tasks) {
    redis_->zrem(redis_keys::kDelayed, json_text);
    const auto parsed = TaskMessageFromJsonString(json_text);
    if (!parsed.Ok()) {
      continue;
    }

    TaskMessage message = parsed.Value();
    message.run_at_ms.reset();
    redis_->lpush(redis_keys::kPending, ToJson(message).dump());
  }
}

std::optional<TaskMessage> RedisBroker::Reserve() {
  auto json_text = redis_->rpop(redis_keys::kPending);
  if (!json_text) {
    return std::nullopt;
  }

  const auto parsed = TaskMessageFromJsonString(*json_text);
  if (!parsed.Ok()) {
    return std::nullopt;
  }

  TaskMessage message = parsed.Value();
  message.status = TaskStatus::kRunning;
  message.reserved_at_ms = NowUnixMs();
  redis_->hset(redis_keys::kRunning, message.id.Value(), ToJson(message).dump());
  redis_->hset(redis_keys::kStatus, message.id.Value(),
               TaskStatusToString(TaskStatus::kRunning));
  return message;
}

int RedisBroker::ReclaimStaleTasks(std::int64_t now_ms,
                                   std::int64_t visibility_timeout_ms) {
  std::unordered_map<std::string, std::string> running_tasks;
  redis_->hgetall(redis_keys::kRunning,
                   std::inserter(running_tasks, running_tasks.end()));

  int reclaimed = 0;
  for (const auto& entry : running_tasks) {
    const auto parsed = TaskMessageFromJsonString(entry.second);
    if (!parsed.Ok() || !parsed.Value().reserved_at_ms.has_value()) {
      continue;
    }

    const TaskMessage& message = parsed.Value();
    if (now_ms - *message.reserved_at_ms <= visibility_timeout_ms) {
      continue;
    }

    TaskMessage requeued = message;
    requeued.status = TaskStatus::kPending;
    requeued.reserved_at_ms.reset();
    redis_->hdel(redis_keys::kRunning, entry.first);
    redis_->lpush(redis_keys::kPending, ToJson(requeued).dump());
    redis_->hset(redis_keys::kStatus, entry.first,
                 TaskStatusToString(TaskStatus::kPending));
    ++reclaimed;
  }

  return reclaimed;
}

void RedisBroker::Ack(const TaskId& id) {
  redis_->hdel(redis_keys::kRunning, id.Value());
  redis_->hset(redis_keys::kStatus, id.Value(),
               TaskStatusToString(TaskStatus::kSucceeded));
  redis_->hset(redis_keys::kResults, id.Value(),
               ToJson(TaskResult::Success()).dump());
  redis_->hdel(redis_keys::kFailures, id.Value());
  RecordCounter(metrics_names::kTasksCompletedTotal);
}

void RedisBroker::Retry(const TaskMessage& task, const std::string& reason) {
  redis_->hdel(redis_keys::kRunning, task.id.Value());

  TaskMessage retry_task = task;
  retry_task.retry_count += 1;
  retry_task.last_error = reason;
  retry_task.status = TaskStatus::kRetrying;
  retry_task.reserved_at_ms.reset();
  retry_task.run_at_ms =
      NowUnixMs() + ComputeRetryDelayMs(retry_task.retry_policy,
                                        retry_task.retry_count);

  const std::string json = ToJson(retry_task).dump();
  redis_->zadd(redis_keys::kDelayed, json,
               static_cast<double>(*retry_task.run_at_ms));
  redis_->hset(redis_keys::kStatus, task.id.Value(),
               TaskStatusToString(TaskStatus::kRetrying));
  redis_->hset(redis_keys::kFailures, task.id.Value(), reason);
  RecordCounter(metrics_names::kTasksRetriedTotal);
}

void RedisBroker::Fail(const TaskId& id, const std::string& reason) {
  TaskMessage message;
  message.id = id;

  const auto running_json = redis_->hget(redis_keys::kRunning, id.Value());
  redis_->hdel(redis_keys::kRunning, id.Value());

  if (running_json) {
    const auto parsed = TaskMessageFromJsonString(*running_json);
    if (parsed.Ok()) {
      message = parsed.Value();
    }
    message.status = TaskStatus::kDead;
    message.last_error = reason;
    redis_->lpush(redis_keys::kDead, ToJson(message).dump());
  }

  redis_->hset(redis_keys::kFailures, id.Value(), reason);
  redis_->hset(redis_keys::kStatus, id.Value(),
               TaskStatusToString(TaskStatus::kDead));
  RecordCounter(metrics_names::kTasksDeadLetteredTotal);
}

ParseResult<TaskStatus> RedisBroker::GetStatus(const TaskId& id) const {
  const auto status = redis_->hget(redis_keys::kStatus, id.Value());
  if (!status) {
    return ParseResult<TaskStatus>::Fail("task not found: " + id.Value());
  }
  return TaskStatusFromString(*status);
}

std::vector<TaskMessage> RedisBroker::ListDeadTasks() const {
  std::vector<std::string> json_items;
  redis_->lrange(redis_keys::kDead, 0, -1, std::back_inserter(json_items));

  std::vector<TaskMessage> tasks;
  tasks.reserve(json_items.size());
  for (const auto& json_text : json_items) {
    const auto parsed = TaskMessageFromJsonString(json_text);
    if (parsed.Ok()) {
      tasks.push_back(parsed.Value());
    }
  }
  return tasks;
}

ParseResult<std::string> RedisBroker::GetFailureReason(
    const TaskId& id) const {
  const auto reason = redis_->hget(redis_keys::kFailures, id.Value());
  if (!reason) {
    return ParseResult<std::string>::Fail("failure reason not found: " +
                                          id.Value());
  }
  return ParseResult<std::string>::Ok(*reason);
}

ParseResult<TaskId> RedisBroker::RetryDeadTask(const TaskId& id) {
  std::vector<std::string> json_items;
  redis_->lrange(redis_keys::kDead, 0, -1, std::back_inserter(json_items));

  for (const auto& json_text : json_items) {
    const auto parsed = TaskMessageFromJsonString(json_text);
    if (!parsed.Ok() || parsed.Value().id != id) {
      continue;
    }

    redis_->lrem(redis_keys::kDead, 0, json_text);

    TaskMessage message = parsed.Value();
    message.status = TaskStatus::kPending;
    message.retry_count = 0;
    message.last_error.clear();
    message.run_at_ms.reset();
    redis_->lpush(redis_keys::kPending, ToJson(message).dump());
    redis_->hset(redis_keys::kStatus, id.Value(),
                 TaskStatusToString(TaskStatus::kPending));
    redis_->hdel(redis_keys::kFailures, id.Value());
    return ParseResult<TaskId>::Ok(id);
  }

  return ParseResult<TaskId>::Fail("dead task not found: " + id.Value());
}

BrokerStats RedisBroker::GetStats() const {
  BrokerStats stats;
  stats.pending_count = static_cast<std::size_t>(redis_->llen(redis_keys::kPending));
  stats.delayed_count = static_cast<std::size_t>(redis_->zcard(redis_keys::kDelayed));
  stats.running_count = static_cast<std::size_t>(redis_->hlen(redis_keys::kRunning));
  stats.dead_count = static_cast<std::size_t>(redis_->llen(redis_keys::kDead));
  return stats;
}

ParseResult<TaskResult> RedisBroker::GetTaskResult(const TaskId& id) const {
  const auto result_json = redis_->hget(redis_keys::kResults, id.Value());
  if (!result_json) {
    return ParseResult<TaskResult>::Fail("task result not found: " + id.Value());
  }

  try {
    return TaskResultFromJson(nlohmann::json::parse(*result_json));
  } catch (const nlohmann::json::parse_error& error) {
    return ParseResult<TaskResult>::Fail(std::string("invalid result json: ") +
                                         error.what());
  }
}

void RedisBroker::UpsertWorkerHeartbeat(const WorkerHeartbeat& heartbeat,
                                        const std::int64_t ttl_seconds) {
  const std::string key = redis_keys::WorkerKey(heartbeat.worker_id);
  redis_->hset(key, "worker_id", heartbeat.worker_id);
  redis_->hset(key, "hostname", heartbeat.hostname);
  redis_->hset(key, "pid", std::to_string(heartbeat.pid));
  redis_->hset(key, "started_at", std::to_string(heartbeat.started_at_ms));
  redis_->hset(key, "last_seen", std::to_string(heartbeat.last_seen_ms));
  redis_->hset(key, "concurrency", std::to_string(heartbeat.concurrency));
  redis_->hset(key, "currently_running",
               std::to_string(heartbeat.currently_running));
  if (ttl_seconds > 0) {
    redis_->expire(key, ttl_seconds);
  }
}

namespace {

std::int64_t ParseInt64Field(const std::unordered_map<std::string, std::string>& fields,
                             const char* key) {
  const auto it = fields.find(key);
  if (it == fields.end()) {
    return 0;
  }
  return std::stoll(it->second);
}

std::size_t ParseSizeField(const std::unordered_map<std::string, std::string>& fields,
                           const char* key) {
  const auto it = fields.find(key);
  if (it == fields.end()) {
    return 0;
  }
  return static_cast<std::size_t>(std::stoull(it->second));
}

WorkerHeartbeat WorkerHeartbeatFromHash(
    const std::unordered_map<std::string, std::string>& fields) {
  WorkerHeartbeat heartbeat;
  const auto worker_id = fields.find("worker_id");
  if (worker_id != fields.end()) {
    heartbeat.worker_id = worker_id->second;
  }
  const auto hostname = fields.find("hostname");
  if (hostname != fields.end()) {
    heartbeat.hostname = hostname->second;
  }
  heartbeat.pid = ParseInt64Field(fields, "pid");
  heartbeat.started_at_ms = ParseInt64Field(fields, "started_at");
  heartbeat.last_seen_ms = ParseInt64Field(fields, "last_seen");
  heartbeat.concurrency = ParseSizeField(fields, "concurrency");
  heartbeat.currently_running = ParseSizeField(fields, "currently_running");
  return heartbeat;
}

}  // namespace

std::vector<WorkerHeartbeat> RedisBroker::ListWorkers() const {
  std::vector<std::string> keys;
  redis_->keys(std::string(redis_keys::kWorkersPrefix) + "*",
               std::back_inserter(keys));

  std::vector<WorkerHeartbeat> workers;
  workers.reserve(keys.size());
  for (const auto& key : keys) {
    std::unordered_map<std::string, std::string> fields;
    redis_->hgetall(key, std::inserter(fields, fields.end()));
    if (fields.empty()) {
      continue;
    }
    workers.push_back(WorkerHeartbeatFromHash(fields));
  }
  return workers;
}

void RedisBroker::RecordRedisOperationError() {
  try {
    redis_->hincrby(redis_keys::kMetrics,
                    CounterField(metrics_names::kRedisOperationErrorsTotal), 1);
  } catch (const sw::redis::Error&) {
  }
}

void RedisBroker::RecordCounter(const std::string& name, const std::int64_t delta) {
  try {
    redis_->hincrby(redis_keys::kMetrics, CounterField(name), delta);
  } catch (const sw::redis::Error&) {
    RecordRedisOperationError();
  }
}

void RedisBroker::RecordDurationMs(const std::string& name,
                                   const std::int64_t duration_ms) {
  try {
    redis_->hincrby(redis_keys::kMetrics, HistogramCountField(name), 1);
    redis_->hincrby(redis_keys::kMetrics, HistogramSumField(name), duration_ms);

    const auto current_max = redis_->hget(redis_keys::kMetrics, HistogramMaxField(name));
    if (!current_max || std::stoll(*current_max) < duration_ms) {
      redis_->hset(redis_keys::kMetrics, HistogramMaxField(name),
                   std::to_string(duration_ms));
    }
  } catch (const sw::redis::Error&) {
    RecordRedisOperationError();
  }
}

MetricsSnapshot RedisBroker::CollectMetrics() const {
  MetricsSnapshot snapshot;
  std::unordered_map<std::string, std::string> fields;
  try {
    redis_->hgetall(redis_keys::kMetrics,
                     std::inserter(fields, fields.end()));
  } catch (const sw::redis::Error&) {
    const_cast<RedisBroker*>(this)->RecordRedisOperationError();
    ApplyLiveGauges(snapshot, *this);
    return snapshot;
  }

  for (const auto& entry : fields) {
    if (StartsWith(entry.first, kCounterPrefix)) {
      snapshot.counters[entry.first.substr(std::char_traits<char>::length(
          kCounterPrefix))] = std::stoll(entry.second);
      continue;
    }

    if (!StartsWith(entry.first, kHistogramPrefix)) {
      continue;
    }

    const std::string histogram_key =
        entry.first.substr(std::char_traits<char>::length(kHistogramPrefix));
    const auto suffix = histogram_key.rfind(':');
    if (suffix == std::string::npos) {
      continue;
    }

    const std::string histogram_name = histogram_key.substr(0, suffix);
    const std::string field_suffix = histogram_key.substr(suffix + 1);
    DurationHistogram& histogram = snapshot.histograms[histogram_name];
    const std::int64_t value = std::stoll(entry.second);
    if (field_suffix == "count") {
      histogram.count = value;
    } else if (field_suffix == "sum") {
      histogram.sum_ms = value;
    } else if (field_suffix == "max") {
      histogram.max_ms = value;
    }
  }

  ApplyLiveGauges(snapshot, *this);
  return snapshot;
}

}  // namespace tq

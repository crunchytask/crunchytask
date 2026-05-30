#include "taskqueue/redis_scripts.h"

#include <string>
#include <vector>

#include <sw/redis++/redis++.h>

#include "taskqueue/redis_keys.h"
#include "taskqueue/task_status.h"

namespace tq {

namespace {

constexpr const char kReserveScript[] = R"(
local json = redis.call('RPOP', KEYS[1])
if not json then
  return nil
end
local ok, msg = pcall(cjson.decode, json)
if not ok or type(msg) ~= 'table' or not msg['id'] then
  return nil
end
msg['status'] = 'running'
msg['reserved_at_ms'] = tonumber(ARGV[1])
local running_json = cjson.encode(msg)
redis.call('HSET', KEYS[2], msg['id'], running_json)
redis.call('HSET', KEYS[3], msg['id'], 'running')
return running_json
)";

constexpr const char kPromoteOneScript[] = R"(
local now_ms = tonumber(ARGV[1])
local items = redis.call('ZRANGEBYSCORE', KEYS[1], '-inf', now_ms, 'LIMIT', 0, 1)
if #items == 0 then
  return 0
end
local member = items[1]
if redis.call('ZREM', KEYS[1], member) == 0 then
  return 0
end
local ok, msg = pcall(cjson.decode, member)
if not ok or type(msg) ~= 'table' then
  return 0
end
msg['run_at_ms'] = nil
msg['status'] = 'pending'
redis.call('LPUSH', KEYS[2], cjson.encode(msg))
return 1
)";

constexpr const char kReclaimOneScript[] = R"(
local task_id = ARGV[1]
local json = redis.call('HGET', KEYS[1], task_id)
if not json then
  return 0
end
local ok, msg = pcall(cjson.decode, json)
if not ok or type(msg) ~= 'table' or not msg['reserved_at_ms'] then
  return 0
end
local now_ms = tonumber(ARGV[2])
local timeout_ms = tonumber(ARGV[3])
if now_ms - tonumber(msg['reserved_at_ms']) <= timeout_ms then
  return 0
end
if redis.call('HDEL', KEYS[1], task_id) == 0 then
  return 0
end
msg['status'] = 'pending'
msg['reserved_at_ms'] = nil
redis.call('LPUSH', KEYS[2], cjson.encode(msg))
redis.call('HSET', KEYS[3], task_id, 'pending')
return 1
)";

constexpr const char kRetryScript[] = R"(
redis.call('HDEL', KEYS[1], ARGV[1])
redis.call('ZADD', KEYS[2], ARGV[3], ARGV[2])
redis.call('HSET', KEYS[3], ARGV[1], ARGV[5])
redis.call('HSET', KEYS[4], ARGV[1], ARGV[4])
return 1
)";

constexpr const char kFailScript[] = R"(
local task_id = ARGV[1]
local json = redis.call('HGET', KEYS[1], task_id)
redis.call('HDEL', KEYS[1], task_id)
if json then
  local ok, msg = pcall(cjson.decode, json)
  if ok and type(msg) == 'table' then
    msg['status'] = 'dead'
    msg['last_error'] = ARGV[2]
    redis.call('LPUSH', KEYS[2], cjson.encode(msg))
  end
end
redis.call('HSET', KEYS[3], task_id, ARGV[2])
redis.call('HSET', KEYS[4], task_id, ARGV[3])
return 1
)";

bool IsNoScriptError(const sw::redis::Error& error) {
  const std::string message = error.what();
  return message.find("NOSCRIPT") != std::string::npos;
}

}  // namespace

RedisScripts::RedisScripts(sw::redis::Redis& redis) : redis_(redis) {
  sources_[static_cast<std::size_t>(ScriptId::kReserve)] = kReserveScript;
  sources_[static_cast<std::size_t>(ScriptId::kPromoteOne)] = kPromoteOneScript;
  sources_[static_cast<std::size_t>(ScriptId::kReclaimOne)] = kReclaimOneScript;
  sources_[static_cast<std::size_t>(ScriptId::kRetry)] = kRetryScript;
  sources_[static_cast<std::size_t>(ScriptId::kFail)] = kFailScript;
  LoadScripts();
}

void RedisScripts::LoadScripts() {
  for (std::size_t index = 0; index < shas_.size(); ++index) {
    LoadScript(static_cast<ScriptId>(index));
  }
}

void RedisScripts::LoadScript(const ScriptId id) {
  const auto index = static_cast<std::size_t>(id);
  shas_[index] = redis_.script_load(sources_[index]);
}

template <typename Result>
Result RedisScripts::Eval(const ScriptId id,
                            const std::initializer_list<std::string_view> keys,
                            const std::vector<std::string>& args) {
  const auto index = static_cast<std::size_t>(id);
  try {
    return redis_.evalsha<Result>(shas_[index], keys.begin(), keys.end(),
                                  args.begin(), args.end());
  } catch (const sw::redis::Error& error) {
    if (!IsNoScriptError(error)) {
      throw;
    }
    LoadScript(id);
    return redis_.evalsha<Result>(shas_[index], keys.begin(), keys.end(),
                                  args.begin(), args.end());
  }
}

std::optional<std::string> RedisScripts::Reserve(const std::int64_t reserved_at_ms) {
  const std::vector<std::string> args = {std::to_string(reserved_at_ms)};
  const auto running_json = Eval<sw::redis::OptionalString>(
      ScriptId::kReserve,
      {redis_keys::kPending, redis_keys::kRunning, redis_keys::kStatus}, args);
  if (!running_json) {
    return std::nullopt;
  }
  return *running_json;
}

int RedisScripts::PromoteOneDueTask(const std::int64_t now_ms) {
  const std::vector<std::string> args = {std::to_string(now_ms)};
  return static_cast<int>(Eval<long long>(
      ScriptId::kPromoteOne, {redis_keys::kDelayed, redis_keys::kPending}, args));
}

int RedisScripts::ReclaimOneStaleTask(const std::string& task_id,
                                      const std::int64_t now_ms,
                                      const std::int64_t visibility_timeout_ms) {
  const std::vector<std::string> args = {task_id, std::to_string(now_ms),
                                         std::to_string(visibility_timeout_ms)};
  return static_cast<int>(Eval<long long>(
      ScriptId::kReclaimOne,
      {redis_keys::kRunning, redis_keys::kPending, redis_keys::kStatus}, args));
}

void RedisScripts::Retry(const std::string& task_id,
                         const std::string& delayed_json, const double run_at_ms,
                         const std::string& reason) {
  const std::vector<std::string> args = {
      task_id, delayed_json, std::to_string(run_at_ms), reason,
      TaskStatusToString(TaskStatus::kRetrying)};
  Eval<long long>(ScriptId::kRetry,
                  {redis_keys::kRunning, redis_keys::kDelayed, redis_keys::kStatus,
                   redis_keys::kFailures},
                  args);
}

void RedisScripts::Fail(const std::string& task_id, const std::string& reason) {
  const std::vector<std::string> args = {task_id, reason,
                                         TaskStatusToString(TaskStatus::kDead)};
  Eval<long long>(ScriptId::kFail,
                  {redis_keys::kRunning, redis_keys::kDead, redis_keys::kFailures,
                   redis_keys::kStatus},
                  args);
}

template sw::redis::OptionalString RedisScripts::Eval<sw::redis::OptionalString>(
    ScriptId, std::initializer_list<std::string_view>, const std::vector<std::string>&);

template long long RedisScripts::Eval<long long>(
    ScriptId, std::initializer_list<std::string_view>, const std::vector<std::string>&);

}  // namespace tq

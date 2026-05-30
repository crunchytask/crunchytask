#ifndef TASKQUEUE_REDIS_SCRIPTS_H_
#define TASKQUEUE_REDIS_SCRIPTS_H_

#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw {
namespace redis {
class Redis;
}  // namespace redis
}  // namespace sw

namespace tq {

class RedisScripts {
 public:
  explicit RedisScripts(sw::redis::Redis& redis);

  std::optional<std::string> Reserve(std::int64_t reserved_at_ms);
  int PromoteOneDueTask(std::int64_t now_ms);
  int ReclaimOneStaleTask(const std::string& task_id, std::int64_t now_ms,
                          std::int64_t visibility_timeout_ms);
  void Retry(const std::string& task_id, const std::string& delayed_json,
             double run_at_ms, const std::string& reason);
  void Fail(const std::string& task_id, const std::string& reason);

 private:
  enum class ScriptId {
    kReserve = 0,
    kPromoteOne,
    kReclaimOne,
    kRetry,
    kFail,
    kCount
  };

  sw::redis::Redis& redis_;
  std::array<std::string, static_cast<std::size_t>(ScriptId::kCount)> shas_;
  std::array<const char*, static_cast<std::size_t>(ScriptId::kCount)> sources_{};

  void LoadScripts();
  void LoadScript(ScriptId id);

  template <typename Result>
  Result Eval(ScriptId id, std::initializer_list<std::string_view> keys,
              const std::vector<std::string>& args);
};

}  // namespace tq

#endif  // TASKQUEUE_REDIS_SCRIPTS_H_

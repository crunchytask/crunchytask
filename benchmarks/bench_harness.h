#ifndef TASKQUEUE_BENCHMARKS_BENCH_HARNESS_H_
#define TASKQUEUE_BENCHMARKS_BENCH_HARNESS_H_

#include <chrono>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace tq {
namespace bench {

using Clock = std::chrono::steady_clock;

inline double DurationMs(const Clock::time_point start,
                         const Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

inline double ThroughputPerSec(const std::int64_t count, const double duration_ms) {
  if (duration_ms <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(count) / (duration_ms / 1000.0);
}

inline nlohmann::json MakeResult(std::string name, std::string status) {
  return nlohmann::json{{"name", std::move(name)}, {"status", std::move(status)}};
}

inline void SetMetric(nlohmann::json& result, const std::string& key,
                      const double value) {
  result[key] = value;
}

inline void SetMetric(nlohmann::json& result, const std::string& key,
                      const std::int64_t value) {
  result[key] = value;
}

inline void SetMetric(nlohmann::json& result, const std::string& key,
                      const std::string& value) {
  result[key] = value;
}

}  // namespace bench
}  // namespace tq

#endif  // TASKQUEUE_BENCHMARKS_BENCH_HARNESS_H_

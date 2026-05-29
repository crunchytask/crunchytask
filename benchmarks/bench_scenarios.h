#ifndef TASKQUEUE_BENCHMARKS_BENCH_SCENARIOS_H_
#define TASKQUEUE_BENCHMARKS_BENCH_SCENARIOS_H_

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tq {
namespace bench {

struct BenchConfig {
  std::string redis_uri;
  std::int64_t iterations = 500;
  std::int64_t warmup = 50;
  std::int64_t visibility_timeout_ms = 1000;
};

std::vector<nlohmann::json> RunAllBenchmarks(const BenchConfig& config);

}  // namespace bench
}  // namespace tq

#endif  // TASKQUEUE_BENCHMARKS_BENCH_SCENARIOS_H_

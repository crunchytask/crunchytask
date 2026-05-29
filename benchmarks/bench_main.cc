#include <cstdlib>
#include <iostream>
#include <string>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include "bench_scenarios.h"
#include "taskqueue/clock.h"
#include "taskqueue/runtime_config.h"

namespace {

std::string RedisUriFromEnv() { return tq::DefaultRedisUriFromEnv(); }

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"taskqueue_bench - operational limit benchmarks"};

  tq::bench::BenchConfig config;
  config.redis_uri = RedisUriFromEnv();

  app.add_option("--redis", config.redis_uri, "Redis connection URI");
  app.add_option("--iterations", config.iterations,
                 "Primary iteration count for throughput benchmarks");
  app.add_option("--warmup", config.warmup, "Warmup iterations before measurement");
  app.add_option("--visibility-timeout-ms", config.visibility_timeout_ms,
                 "Visibility timeout used by stale reclaim benchmark");

  CLI11_PARSE(app, argc, argv);

  nlohmann::json output;
  output["metadata"] = {
      {"redis_uri", config.redis_uri},
      {"iterations", config.iterations},
      {"warmup", config.warmup},
      {"visibility_timeout_ms", config.visibility_timeout_ms},
      {"timestamp_ms", tq::NowUnixMs()},
  };
  output["benchmarks"] = tq::bench::RunAllBenchmarks(config);

  std::cout << output.dump(2) << '\n';
  return 0;
}

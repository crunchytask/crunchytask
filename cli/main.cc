#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include "taskqueue/metrics.h"
#include "taskqueue/metrics_snapshot.h"
#include "taskqueue/runtime_config.h"
#include "taskqueue/task_json.h"
#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"
#include "taskqueue/task_status.h"
#include "taskqueue/version.h"
#include "taskqueue/worker.h"
#include "taskqueue/worker_heartbeat.h"

#ifdef TASKQUEUE_HAS_REDIS
#include "taskqueue/redis_broker.h"
#include "taskqueue/task_id.h"
#endif

namespace {

#ifdef TASKQUEUE_HAS_REDIS
tq::Worker* g_worker = nullptr;

void HandleStopSignal(int /*signal*/) {
  if (g_worker != nullptr) {
    g_worker->Stop();
  }
}

void ExitWithError(const std::string& message);

void ValidateOrExit(const tq::ConfigValidation& validation) {
  if (!validation.Ok()) {
    ExitWithError(validation.Error());
  }
}

bool EnsureRedis(const std::string& redis_uri) {
  if (tq::RedisBroker::Ping(redis_uri)) {
    return true;
  }

  std::cerr << "Redis not available at " << redis_uri << '\n';
  std::cerr << "Start Redis with: docker compose up -d redis\n";
  return false;
}

void ExitWithError(const std::string& message) {
  std::cerr << message << '\n';
  std::exit(1);
}

nlohmann::json ParsePayloadJson(const std::string& payload_json) {
  try {
    return nlohmann::json::parse(payload_json);
  } catch (const nlohmann::json::parse_error& error) {
    ExitWithError(std::string("invalid JSON payload: ") + error.what());
  }
  return nlohmann::json::object();
}

tq::TaskHandler MakeAddHandler() {
  return [](const nlohmann::json& payload) {
    const int a = payload.at("a").get<int>();
    const int b = payload.at("b").get<int>();
    return tq::TaskResult::Success({{"result", a + b}});
  };
}

nlohmann::json BuildTaskStatusJson(tq::RedisBroker& broker, const tq::TaskId& id) {
  const auto status = broker.GetStatus(id);
  if (!status.Ok()) {
    ExitWithError(status.Error());
  }

  nlohmann::json json{
      {"task_id", id.Value()},
      {"status", tq::TaskStatusToString(status.Value())},
      {"failure_reason", nullptr},
      {"result", nullptr},
  };

  const auto reason = broker.GetFailureReason(id);
  if (reason.Ok()) {
    json["failure_reason"] = reason.Value();
  }

  const auto result = broker.GetTaskResult(id);
  if (result.Ok()) {
    json["result"] = tq::ToJson(result.Value());
  }

  return json;
}

void PrintTaskStatus(tq::RedisBroker& broker, const tq::TaskId& id) {
  const nlohmann::json json = BuildTaskStatusJson(broker, id);
  std::cout << "task_id: " << json.at("task_id").get<std::string>() << '\n';
  std::cout << "status: " << json.at("status").get<std::string>() << '\n';
  if (!json.at("failure_reason").is_null()) {
    std::cout << "failure_reason: " << json.at("failure_reason").get<std::string>()
              << '\n';
  }
  if (!json.at("result").is_null()) {
    std::cout << "result: "
              << json.at("result").at("payload").dump() << '\n';
  }
}

void PrintTaskResult(tq::RedisBroker& broker, const tq::TaskId& id) {
  const auto result = broker.GetTaskResult(id);
  if (!result.Ok()) {
    ExitWithError(result.Error());
  }

  std::cout << result.Value().payload.dump() << '\n';
}

void PrintStats(const tq::BrokerStats& stats) {
  std::cout << "pending: " << stats.pending_count << '\n';
  std::cout << "delayed: " << stats.delayed_count << '\n';
  std::cout << "running: " << stats.running_count << '\n';
  std::cout << "dead: " << stats.dead_count << '\n';
}

void PrintWorkers(const std::vector<tq::WorkerHeartbeat>& workers) {
  if (workers.empty()) {
    std::cout << "No active workers\n";
    return;
  }

  std::vector<tq::WorkerHeartbeat> sorted = workers;
  std::sort(sorted.begin(), sorted.end(),
            [](const tq::WorkerHeartbeat& left,
               const tq::WorkerHeartbeat& right) {
              if (left.hostname != right.hostname) {
                return left.hostname < right.hostname;
              }
              return left.worker_id < right.worker_id;
            });

  std::cout << "worker_id\thostname\tpid\tconcurrency\trunning\tstarted_at\tlast_seen\n";
  for (const tq::WorkerHeartbeat& worker : sorted) {
    std::cout << worker.worker_id << '\t' << worker.hostname << '\t'
              << worker.pid << '\t' << worker.concurrency << '\t'
              << worker.currently_running << '\t' << worker.started_at_ms << '\t'
              << worker.last_seen_ms << '\n';
  }
}
#endif

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"taskq - distributed task queue CLI"};
  app.set_version_flag("-V,--version", std::string(tq::kVersionString));

#ifdef TASKQUEUE_HAS_REDIS
  std::string redis_uri = tq::DefaultRedisUriFromEnv();

  auto* enqueue_cmd = app.add_subcommand("enqueue", "Enqueue a task");
  std::string task_name;
  std::string payload_json = "{}";
  std::string retry_policy_json;
  std::int64_t delay_ms = 0;
  enqueue_cmd->add_option("task_name", task_name, "Task name")->required();
  enqueue_cmd->add_option("--payload", payload_json, "JSON payload");
  enqueue_cmd->add_option("--delay-ms", delay_ms,
                          "Delay task execution by this many milliseconds");
  enqueue_cmd->add_option("--retry-policy", retry_policy_json,
                          "JSON retry policy object");
  enqueue_cmd->add_option("--redis", redis_uri, "Redis connection URI");
  enqueue_cmd->callback([&]() {
    ValidateOrExit(tq::ValidateRedisUri(redis_uri));
    ValidateOrExit(tq::ValidateDelayMs(delay_ms));
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    const nlohmann::json payload = ParsePayloadJson(payload_json);
    tq::TaskMessage task =
        delay_ms > 0 ? tq::TaskMessage::CreateWithDelay(task_name, payload, delay_ms)
                     : tq::TaskMessage::Create(task_name, payload);

    if (!retry_policy_json.empty()) {
      const auto parsed_policy =
          tq::RetryPolicyFromJson(ParsePayloadJson(retry_policy_json));
      if (!parsed_policy.Ok()) {
        ExitWithError(parsed_policy.Error());
      }
      task.retry_policy = parsed_policy.Value();
    }

    tq::RedisBroker broker(redis_uri);
    const tq::TaskId id = broker.Enqueue(task);
    std::cout << id.Value() << '\n';
  });

  auto* status_cmd = app.add_subcommand("status", "Show task status");
  std::string status_task_id;
  std::string status_format = "text";
  status_cmd->add_option("task_id", status_task_id, "Task id")->required();
  status_cmd->add_option("--format", status_format, "Output format: text or json");
  status_cmd->add_option("--redis", redis_uri, "Redis connection URI");
  status_cmd->callback([&]() {
    ValidateOrExit(tq::ValidateRedisUri(redis_uri));
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    const auto parsed_id = tq::TaskId::Parse(status_task_id);
    if (!parsed_id.Ok()) {
      ExitWithError(parsed_id.Error());
    }

    tq::RedisBroker broker(redis_uri);
    if (status_format == "json") {
      std::cout << BuildTaskStatusJson(broker, parsed_id.Value()).dump() << '\n';
      return;
    }
    if (status_format != "text") {
      ExitWithError("unsupported status format: " + status_format);
    }

    PrintTaskStatus(broker, parsed_id.Value());
  });

  auto* result_cmd = app.add_subcommand("result", "Show task result payload");
  std::string result_task_id;
  std::string result_format = "text";
  result_cmd->add_option("task_id", result_task_id, "Task id")->required();
  result_cmd->add_option("--format", result_format,
                         "Output format: text or json");
  result_cmd->add_option("--redis", redis_uri, "Redis connection URI");
  result_cmd->callback([&]() {
    ValidateOrExit(tq::ValidateRedisUri(redis_uri));
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    const auto parsed_id = tq::TaskId::Parse(result_task_id);
    if (!parsed_id.Ok()) {
      ExitWithError(parsed_id.Error());
    }

    tq::RedisBroker broker(redis_uri);
    const auto task_result = broker.GetTaskResult(parsed_id.Value());
    if (!task_result.Ok()) {
      ExitWithError(task_result.Error());
    }

    if (result_format == "json") {
      std::cout << tq::ToJson(task_result.Value()).dump() << '\n';
      return;
    }
    if (result_format != "text") {
      ExitWithError("unsupported result format: " + result_format);
    }

    PrintTaskResult(broker, parsed_id.Value());
  });

  auto* stats_cmd = app.add_subcommand("stats", "Show queue statistics");
  stats_cmd->add_option("--redis", redis_uri, "Redis connection URI");
  stats_cmd->callback([&]() {
    ValidateOrExit(tq::ValidateRedisUri(redis_uri));
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    tq::RedisBroker broker(redis_uri);
    PrintStats(broker.GetStats());
  });

  auto* worker_cmd = app.add_subcommand("worker", "Worker commands");
  auto* start_cmd = worker_cmd->add_subcommand("start", "Start the task worker");

  std::size_t concurrency = 4;
  std::int64_t visibility_timeout_ms = tq::Worker::kDefaultVisibilityTimeoutMs;
  start_cmd->add_option("--redis", redis_uri, "Redis connection URI");
  start_cmd->add_option("--concurrency", concurrency,
                        "Number of worker threads");
  start_cmd->add_option("--visibility-timeout-ms", visibility_timeout_ms,
                        "Requeue running tasks after this timeout (ms)");

  start_cmd->callback([&]() {
    ValidateOrExit(tq::ValidateRedisUri(redis_uri));
    ValidateOrExit(tq::ValidateWorkerConcurrency(concurrency));
    ValidateOrExit(tq::ValidateVisibilityTimeout(
        std::chrono::milliseconds(visibility_timeout_ms)));
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    tq::RedisBroker broker(redis_uri);
    tq::Worker worker(broker, concurrency);
    worker.SetVisibilityTimeout(
        std::chrono::milliseconds(visibility_timeout_ms));
    worker.RegisterTask("add", MakeAddHandler());

    g_worker = &worker;
    std::signal(SIGINT, HandleStopSignal);
    std::signal(SIGTERM, HandleStopSignal);

    worker.Run();
    g_worker = nullptr;
  });

  auto* failed_cmd = app.add_subcommand("failed", "Dead-letter queue commands");
  auto* list_cmd = failed_cmd->add_subcommand("list", "List failed tasks");
  std::string failed_list_format = "text";
  list_cmd->add_option("--format", failed_list_format,
                       "Output format: text or json");
  list_cmd->add_option("--redis", redis_uri, "Redis connection URI");

  list_cmd->callback([&]() {
    ValidateOrExit(tq::ValidateRedisUri(redis_uri));
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    tq::RedisBroker broker(redis_uri);
    const auto dead_tasks = broker.ListDeadTasks();
    if (failed_list_format == "json") {
      nlohmann::json json = nlohmann::json::array();
      for (const tq::TaskMessage& task : dead_tasks) {
        json.push_back(tq::ToJson(task));
      }
      std::cout << json.dump() << '\n';
      return;
    }
    if (failed_list_format != "text") {
      ExitWithError("unsupported failed list format: " + failed_list_format);
    }

    if (dead_tasks.empty()) {
      std::cout << "No failed tasks\n";
      return;
    }

    for (const tq::TaskMessage& task : dead_tasks) {
      const auto reason = broker.GetFailureReason(task.id);
      const std::string reason_text = reason.Ok() ? reason.Value() : "unknown";
      std::cout << task.id.Value() << '\t' << task.name << '\t'
                << task.retry_count << '\t' << reason_text << '\n';
    }
  });

  auto* retry_cmd =
      failed_cmd->add_subcommand("retry", "Retry a failed task by id");
  std::string task_id;
  retry_cmd->add_option("task_id", task_id, "Task id to retry")->required();
  retry_cmd->add_option("--redis", redis_uri, "Redis connection URI");

  retry_cmd->callback([&]() {
    ValidateOrExit(tq::ValidateRedisUri(redis_uri));
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    const auto parsed_id = tq::TaskId::Parse(task_id);
    if (!parsed_id.Ok()) {
      ExitWithError(parsed_id.Error());
    }

    tq::RedisBroker broker(redis_uri);
    const auto retried = broker.RetryDeadTask(parsed_id.Value());
    if (!retried.Ok()) {
      ExitWithError(retried.Error());
    }

    const auto status = broker.GetStatus(retried.Value());
    if (status.Ok()) {
      std::cout << "requeued task " << retried.Value().Value() << " status="
                << tq::TaskStatusToString(status.Value()) << '\n';
    }
  });

  auto* workers_cmd = app.add_subcommand("workers", "Worker discovery commands");
  auto* workers_list_cmd =
      workers_cmd->add_subcommand("list", "List active workers");
  workers_list_cmd->add_option("--redis", redis_uri, "Redis connection URI");
  workers_list_cmd->callback([&]() {
    ValidateOrExit(tq::ValidateRedisUri(redis_uri));
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    tq::RedisBroker broker(redis_uri);
    PrintWorkers(broker.ListWorkers());
  });

  auto* metrics_cmd = app.add_subcommand("metrics", "Show task queue metrics");
  std::string metrics_format = "prometheus";
  metrics_cmd->add_option("--redis", redis_uri, "Redis connection URI");
  metrics_cmd->add_option("--format", metrics_format,
                          "Output format: prometheus or plain");
  metrics_cmd->callback([&]() {
    ValidateOrExit(tq::ValidateRedisUri(redis_uri));
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    tq::RedisBroker broker(redis_uri);
    const tq::MetricsSnapshot snapshot = tq::CollectMetrics(broker);
    if (metrics_format == "plain") {
      std::cout << tq::FormatMetricsPlainText(snapshot);
      return;
    }
    if (metrics_format == "prometheus") {
      std::cout << tq::FormatMetricsPrometheus(snapshot);
      return;
    }

    ExitWithError("unsupported metrics format: " + metrics_format);
  });
#else
  app.add_subcommand("enqueue", "Enqueue tasks (Redis support disabled)")
      ->callback([]() {
        std::cerr << "Redis support disabled. Rebuild with "
                     "TASKQUEUE_ENABLE_REDIS=ON\n";
        std::exit(1);
      });
#endif

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }

  if (app.get_subcommands().empty()) {
    std::cout << app.help() << '\n';
  }

  return 0;
}

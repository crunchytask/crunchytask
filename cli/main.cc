#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include "taskqueue/task_message.h"
#include "taskqueue/task_result.h"
#include "taskqueue/task_status.h"
#include "taskqueue/version.h"
#include "taskqueue/worker.h"

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

std::string RedisUri() {
  const char* uri = std::getenv("TASKQUEUE_REDIS_URI");
  if (uri != nullptr && uri[0] != '\0') {
    return uri;
  }
  return "tcp://127.0.0.1:6379";
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

void PrintTaskStatus(tq::RedisBroker& broker, const tq::TaskId& id) {
  const auto status = broker.GetStatus(id);
  if (!status.Ok()) {
    ExitWithError(status.Error());
  }

  std::cout << "task_id: " << id.Value() << '\n';
  std::cout << "status: " << tq::TaskStatusToString(status.Value()) << '\n';

  const auto reason = broker.GetFailureReason(id);
  if (reason.Ok()) {
    std::cout << "failure_reason: " << reason.Value() << '\n';
  }

  const auto result = broker.GetTaskResult(id);
  if (result.Ok()) {
    std::cout << "result: " << result.Value().payload.dump() << '\n';
  }
}

void PrintStats(const tq::BrokerStats& stats) {
  std::cout << "pending: " << stats.pending_count << '\n';
  std::cout << "delayed: " << stats.delayed_count << '\n';
  std::cout << "running: " << stats.running_count << '\n';
  std::cout << "dead: " << stats.dead_count << '\n';
}
#endif

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"taskq - distributed task queue CLI"};
  app.set_version_flag("-V,--version", std::string(tq::kVersionString));

#ifdef TASKQUEUE_HAS_REDIS
  std::string redis_uri = RedisUri();

  auto* enqueue_cmd = app.add_subcommand("enqueue", "Enqueue a task");
  std::string task_name;
  std::string payload_json = "{}";
  std::int64_t delay_ms = 0;
  enqueue_cmd->add_option("task_name", task_name, "Task name")->required();
  enqueue_cmd->add_option("--payload", payload_json, "JSON payload");
  enqueue_cmd->add_option("--delay-ms", delay_ms,
                          "Delay task execution by this many milliseconds");
  enqueue_cmd->add_option("--redis", redis_uri, "Redis connection URI");
  enqueue_cmd->callback([&]() {
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    const nlohmann::json payload = ParsePayloadJson(payload_json);
    tq::TaskMessage task =
        delay_ms > 0 ? tq::TaskMessage::CreateWithDelay(task_name, payload, delay_ms)
                     : tq::TaskMessage::Create(task_name, payload);

    tq::RedisBroker broker(redis_uri);
    const tq::TaskId id = broker.Enqueue(task);
    std::cout << id.Value() << '\n';
  });

  auto* status_cmd = app.add_subcommand("status", "Show task status");
  std::string status_task_id;
  status_cmd->add_option("task_id", status_task_id, "Task id")->required();
  status_cmd->add_option("--redis", redis_uri, "Redis connection URI");
  status_cmd->callback([&]() {
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    const auto parsed_id = tq::TaskId::Parse(status_task_id);
    if (!parsed_id.Ok()) {
      ExitWithError(parsed_id.Error());
    }

    tq::RedisBroker broker(redis_uri);
    PrintTaskStatus(broker, parsed_id.Value());
  });

  auto* stats_cmd = app.add_subcommand("stats", "Show queue statistics");
  stats_cmd->add_option("--redis", redis_uri, "Redis connection URI");
  stats_cmd->callback([&]() {
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
  list_cmd->add_option("--redis", redis_uri, "Redis connection URI");

  list_cmd->callback([&]() {
    if (!EnsureRedis(redis_uri)) {
      std::exit(1);
    }

    tq::RedisBroker broker(redis_uri);
    const auto dead_tasks = broker.ListDeadTasks();
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

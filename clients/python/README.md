# CrunchyTask Python client

Minimal **producer/status** client for CrunchyTask. It does **not** talk to Redis directly or duplicate broker internals. All queue operations go through the C++ `taskq` CLI, which owns enqueue routing, Redis keys, and status/result lookups.

This package does **not** implement a Python worker.

## Prerequisites

Build or install `taskq` and ensure it is on `PATH`, or set `TASKQUEUE_TASKQ_BIN`:

```bash
cmake -S . -B build
cmake --build build
export PATH="$PWD/build:$PATH"
```

## Install

```bash
cd clients/python
uv sync --extra dev
```

## Usage

Start Redis and a C++ worker first:

```bash
docker compose up -d redis
./build/taskq worker start --concurrency 4
```

Enqueue and inspect tasks from Python:

```python
from crunchytask import TaskQueueClient

client = TaskQueueClient(taskq_bin="./build/taskq")

task_id = client.enqueue(
    "add",
    {"a": 2, "b": 3},
    delay_seconds=0,
)

print("task_id:", task_id)
print("status:", client.status(task_id))

# After the worker completes the task:
print("result:", client.result(task_id))

# Inspect dead-letter queue entries (full task JSON blobs):
for task in client.failed_list():
    print(task["id"], task.get("last_error"))
```

Delayed enqueue (5 seconds):

```python
task_id = client.enqueue("add", {"a": 10, "b": 1}, delay_seconds=5)
```

Custom retry policy:

```python
task_id = client.enqueue(
    "add",
    {"a": 1},
    retry_policy={"max_retries": 0, "base_delay_ms": 1000, "multiplier": 2.0},
)
```

## Configuration

| Variable | Default | Notes |
|----------|---------|-------|
| `TASKQUEUE_REDIS_URI` | `tcp://127.0.0.1:6379` | Passed through to `taskq --redis` |
| `TASKQUEUE_TASKQ_BIN` | `taskq` on `PATH` | Path to the C++ CLI binary |

The `queue` argument is accepted for API symmetry but only `"default"` is supported today (matching the single-queue C++ broker).

## Wire format tests

`tests/test_wire_format.py` documents the task JSON schema (`schema_version: 1`) against fixtures from the C++ `task_json` unit tests. Payload construction and Redis layout remain owned by the C++ broker/CLI.

## Test

```bash
cd clients/python
uv run pytest
```

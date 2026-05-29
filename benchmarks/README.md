# CrunchyTask benchmarks

Operational limit benchmarks for enqueue throughput, worker completion, retry
overhead, delayed scheduling latency, and stale task reclaim latency.

Benchmarks use the public `taskqueue` library API and Redis. They are **not**
part of the default `check` test target.

## Prerequisites

- Built with benchmarks enabled and Redis support on
- Redis running locally (`docker compose up -d redis`)

## Build

```bash
cmake -S . -B build -DTASKQUEUE_BUILD_BENCHMARKS=ON
cmake --build build --target taskqueue_bench
```

## Run

```bash
export TASKQUEUE_REDIS_URI=tcp://127.0.0.1:6379   # optional
./build/taskqueue_bench
```

Options:

| Flag | Default | Description |
|------|---------|-------------|
| `--redis` | `TASKQUEUE_REDIS_URI` or `tcp://127.0.0.1:6379` | Redis URI |
| `--iterations` | `500` | Throughput benchmark iterations |
| `--warmup` | `50` | Enqueue warmup iterations |
| `--visibility-timeout-ms` | `1000` | Stale reclaim visibility timeout |

Output is JSON on stdout (machine-readable). Example:

```json
{
  "metadata": { "redis_uri": "tcp://127.0.0.1:6379", "iterations": 500 },
  "benchmarks": [
    {
      "name": "enqueue_throughput",
      "status": "ok",
      "duration_ms": 42.1,
      "throughput_per_sec": 11876.5
    }
  ]
}
```

## Convenience script

```bash
./benchmarks/run_bench.sh
```

## Benchmarks

| Name | Measures |
|------|----------|
| `enqueue_throughput` | Tasks/sec for `Enqueue` |
| `worker_completion_throughput` | End-to-end drain rate with a noop worker |
| `retry_overhead` | Extra cost of `Retry` vs `Ack` per operation |
| `delayed_task_scheduling_latency` | Mean ms from due time to pending promotion |
| `stale_reclaim_latency` | Mean ms for `ReclaimStaleTasks` on one stale task |

If Redis is unavailable, individual benchmarks report `"status": "skipped"`.

# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **Phase B atomicity polish** — Lua `Ack` and `RetryDeadTask`; enqueue uses `MULTI`/`EXEC`; promote due tasks now updates `taskq:status` together with moving work to pending.

### Added

- Integration tests for atomic ack, duplicate dead-letter retry, and promote-after-retry status coherence.

### Planned for v0.1.1

- Release hygiene and documentation updates from post-v0.1.0 review.

## [0.1.0] - 2026-05-30

First public release.

### Added

- C++20 task queue library (`libtaskqueue`) with Redis broker.
- **Phase A atomic broker transitions** — Lua scripts for enqueue, reserve, ack, retry, fail, and related state moves so queue keys stay consistent under normal operation.
- Worker runtime with thread pool, task registry, retries with exponential backoff, delayed tasks, visibility timeout / crash recovery, and dead-letter queue.
- `taskq` CLI: enqueue, status, result, stats, worker start, failed list/retry, workers list, metrics (Prometheus or plain text).
- Worker heartbeats stored in Redis (`taskq:workers:<id>`).
- In-process and Redis-backed metrics counters and histograms.
- Operational benchmarks under `benchmarks/` (throughput, retry overhead, scheduling/reclaim latency).
- Python producer client (`clients/python`) wrapping the CLI for enqueue and inspection.
- Docker Compose file for local Redis.
- Catch2 test suite (~100 tests), including integration tests against Redis and atomicity coverage for broker transitions.

### Notes

- Delivery guarantee is **at-least-once**; handlers should be idempotent.
- See [README.md](README.md) for build, quick start, and CLI reference.

[Unreleased]: https://github.com/crunchytask/crunchytask/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/crunchytask/crunchytask/releases/tag/v0.1.0

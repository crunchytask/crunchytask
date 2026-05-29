#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cmake -S "${ROOT}" -B "${ROOT}/build" -DTASKQUEUE_BUILD_BENCHMARKS=ON
cmake --build "${ROOT}/build" --target taskqueue_bench

docker compose -f "${ROOT}/docker-compose.yml" up -d redis

export TASKQUEUE_REDIS_URI="${TASKQUEUE_REDIS_URI:-tcp://127.0.0.1:6379}"
"${ROOT}/build/taskqueue_bench" "$@"

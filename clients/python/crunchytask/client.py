"""Producer client that delegates broker operations to the taskq CLI."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from collections.abc import Mapping, Sequence
from typing import Any

from crunchytask.wire import is_valid_task_id

DEFAULT_REDIS_URI = "tcp://127.0.0.1:6379"
SUPPORTED_QUEUE = "default"


class TaskQueueError(Exception):
    """Raised when a task queue operation fails."""


class TaskQueueClient:
    """Producer/status client for CrunchyTask (enqueue and inspect only)."""

    def __init__(
        self,
        redis_url: str | None = None,
        *,
        taskq_bin: str | None = None,
    ) -> None:
        self._redis_url = (
            redis_url or os.getenv("TASKQUEUE_REDIS_URI") or DEFAULT_REDIS_URI
        )
        self._taskq = (
            taskq_bin
            or os.getenv("TASKQUEUE_TASKQ_BIN")
            or shutil.which("taskq")
            or "taskq"
        )

    def enqueue(
        self,
        task_name: str,
        args: Mapping[str, Any],
        *,
        queue: str = SUPPORTED_QUEUE,
        delay_seconds: float = 0,
        retry_policy: Mapping[str, Any] | None = None,
    ) -> str:
        if queue != SUPPORTED_QUEUE:
            raise ValueError(
                f"unsupported queue {queue!r}; only {SUPPORTED_QUEUE!r} is available"
            )
        if delay_seconds < 0:
            raise ValueError("delay_seconds must be >= 0")

        command = [
            self._taskq,
            "enqueue",
            task_name,
            "--payload",
            json.dumps(dict(args), separators=(",", ":")),
            "--redis",
            self._redis_url,
        ]
        if delay_seconds > 0:
            command.extend(["--delay-ms", str(int(delay_seconds * 1000))])
        if retry_policy is not None:
            command.extend(
                [
                    "--retry-policy",
                    json.dumps(dict(retry_policy), separators=(",", ":")),
                ]
            )

        task_id = self._run(command).strip()
        validate_task_id(task_id)
        return task_id

    def status(self, task_id: str) -> str:
        validate_task_id(task_id)
        payload = self._run_json(
            [
                self._taskq,
                "status",
                task_id,
                "--format",
                "json",
                "--redis",
                self._redis_url,
            ]
        )
        status = payload.get("status")
        if not isinstance(status, str):
            raise TaskQueueError("taskq status response missing status field")
        return status

    def result(self, task_id: str) -> dict[str, Any]:
        validate_task_id(task_id)
        payload = self._run_json(
            [
                self._taskq,
                "result",
                task_id,
                "--format",
                "json",
                "--redis",
                self._redis_url,
            ]
        )
        if not isinstance(payload, dict):
            raise TaskQueueError("task result must be an object")

        for field in ("success", "payload", "error_message"):
            if field not in payload:
                raise TaskQueueError(f"missing required result field: {field}")

        return payload

    def failed_list(self) -> list[dict[str, Any]]:
        payload = self._run_json(
            [
                self._taskq,
                "failed",
                "list",
                "--format",
                "json",
                "--redis",
                self._redis_url,
            ]
        )
        if not isinstance(payload, list):
            raise TaskQueueError("failed list response must be a JSON array")
        return payload

    def _run_json(self, command: Sequence[str]) -> Any:
        return json.loads(self._run(command))

    def _run(self, command: Sequence[str]) -> str:
        try:
            completed = subprocess.run(
                command,
                check=False,
                capture_output=True,
                text=True,
            )
        except OSError as exc:
            raise TaskQueueError(f"failed to run taskq: {exc}") from exc

        if completed.returncode != 0:
            message = (
                completed.stderr.strip()
                or completed.stdout.strip()
                or "taskq command failed"
            )
            raise TaskQueueError(message)

        return completed.stdout


def validate_task_id(task_id: str) -> None:
    if not is_valid_task_id(task_id):
        raise ValueError("task id must be a UUID string")

"""Documented task JSON wire schema (matches C++ task_json)."""

from __future__ import annotations

import json
import re
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Any, Mapping

SCHEMA_VERSION = 1

DEFAULT_RETRY_POLICY: dict[str, int | float] = {
    "max_retries": 3,
    "base_delay_ms": 1000,
    "multiplier": 2.0,
}

UUID_PATTERN = re.compile(
    r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
)


@dataclass(frozen=True)
class RetryPolicy:
    max_retries: int = 3
    base_delay_ms: int = 1000
    multiplier: float = 2.0

    def to_wire(self) -> dict[str, int | float]:
        return {
            "max_retries": self.max_retries,
            "base_delay_ms": self.base_delay_ms,
            "multiplier": self.multiplier,
        }


def now_unix_ms() -> int:
    return int(datetime.now(tz=UTC).timestamp() * 1000)


def generate_task_id() -> str:
    return str(uuid.uuid4())


def is_valid_task_id(task_id: str) -> bool:
    return bool(UUID_PATTERN.match(task_id))


def parse_retry_policy(raw: Mapping[str, Any] | None) -> RetryPolicy:
    if raw is None:
        return RetryPolicy()

    policy = RetryPolicy(
        max_retries=int(raw.get("max_retries", DEFAULT_RETRY_POLICY["max_retries"])),
        base_delay_ms=int(
            raw.get("base_delay_ms", DEFAULT_RETRY_POLICY["base_delay_ms"])
        ),
        multiplier=float(raw.get("multiplier", DEFAULT_RETRY_POLICY["multiplier"])),
    )
    validate_retry_policy(policy)
    return policy


def validate_retry_policy(policy: RetryPolicy) -> None:
    if policy.max_retries < 0:
        raise ValueError("retry max_retries must be >= 0")
    if policy.base_delay_ms < 0:
        raise ValueError("retry base_delay_ms must be >= 0")
    if policy.multiplier < 0:
        raise ValueError("retry multiplier must be >= 0")


def build_task_message(
    *,
    task_name: str,
    args: Mapping[str, Any],
    task_id: str | None = None,
    created_at_ms: int | None = None,
    delay_seconds: float = 0,
    retry_policy: Mapping[str, Any] | RetryPolicy | None = None,
) -> dict[str, Any]:
    if delay_seconds < 0:
        raise ValueError("delay_seconds must be >= 0")

    if isinstance(retry_policy, RetryPolicy):
        policy = retry_policy
    else:
        policy = parse_retry_policy(retry_policy)

    resolved_id = task_id or generate_task_id()
    if not is_valid_task_id(resolved_id):
        raise ValueError("task id must be a UUID string")

    created = created_at_ms if created_at_ms is not None else now_unix_ms()
    delay_ms = int(delay_seconds * 1000)

    message: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "id": resolved_id,
        "name": task_name,
        "payload": dict(args),
        "status": "pending",
        "retry_count": 0,
        "retry_policy": policy.to_wire(),
        "created_at_ms": created,
    }

    if delay_ms > 0:
        message["run_at_ms"] = created + delay_ms

    return message


def serialize_task_message(message: Mapping[str, Any]) -> str:
    return json.dumps(message, separators=(",", ":"), ensure_ascii=False)

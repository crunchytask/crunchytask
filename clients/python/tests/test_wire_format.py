import json

from crunchytask.wire import (
    SCHEMA_VERSION,
    build_task_message,
    serialize_task_message,
)
from tests.fixtures import CPP_IMMEDIATE_MESSAGE, CPP_SAMPLE_MESSAGE


def test_build_task_message_matches_cpp_delayed_fixture() -> None:
    message = build_task_message(
        task_name="add",
        args={"a": 2, "b": 3},
        task_id="550e8400-e29b-41d4-a716-446655440000",
        created_at_ms=1717000000000,
        delay_seconds=1.0,
    )

    assert message == CPP_SAMPLE_MESSAGE


def test_build_task_message_matches_cpp_immediate_fixture() -> None:
    message = build_task_message(
        task_name="add",
        args={"a": 2, "b": 3},
        task_id="550e8400-e29b-41d4-a716-446655440000",
        created_at_ms=1717000000000,
    )

    assert message == CPP_IMMEDIATE_MESSAGE


def test_schema_version_is_always_one() -> None:
    message = build_task_message(task_name="add", args={"a": 1})
    assert message["schema_version"] == SCHEMA_VERSION == 1


def test_serialized_payload_is_compact_json() -> None:
    text = serialize_task_message(CPP_IMMEDIATE_MESSAGE)
    assert text == json.dumps(CPP_IMMEDIATE_MESSAGE, separators=(",", ":"))


def test_custom_retry_policy_is_embedded() -> None:
    message = build_task_message(
        task_name="add",
        args={},
        retry_policy={"max_retries": 0, "base_delay_ms": 500, "multiplier": 1.5},
    )
    assert message["retry_policy"] == {
        "max_retries": 0,
        "base_delay_ms": 500,
        "multiplier": 1.5,
    }

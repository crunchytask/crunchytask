import json
from subprocess import CompletedProcess

import pytest

from crunchytask.client import TaskQueueClient, TaskQueueError
from tests.fixtures import CPP_IMMEDIATE_MESSAGE, CPP_TASK_RESULT_SUCCESS


def run_result(returncode: int, stdout: str = "", stderr: str = "") -> CompletedProcess[str]:
    return CompletedProcess(args=[], returncode=returncode, stdout=stdout, stderr=stderr)


def test_enqueue_delegates_to_taskq(monkeypatch: pytest.MonkeyPatch) -> None:
    calls: list[list[str]] = []

    def fake_run(command, **kwargs):
        calls.append(list(command))
        return run_result(0, stdout="550e8400-e29b-41d4-a716-446655440000\n")

    monkeypatch.setattr("crunchytask.client.subprocess.run", fake_run)

    client = TaskQueueClient(
        redis_url="tcp://127.0.0.1:6379",
        taskq_bin="/usr/bin/taskq",
    )
    task_id = client.enqueue("add", {"a": 2, "b": 3}, delay_seconds=2.5)

    assert task_id == "550e8400-e29b-41d4-a716-446655440000"
    assert calls == [
        [
            "/usr/bin/taskq",
            "enqueue",
            "add",
            "--payload",
            '{"a":2,"b":3}',
            "--redis",
            "tcp://127.0.0.1:6379",
            "--delay-ms",
            "2500",
        ]
    ]


def test_enqueue_passes_retry_policy(monkeypatch: pytest.MonkeyPatch) -> None:
    calls: list[list[str]] = []

    def fake_run(command, **kwargs):
        calls.append(list(command))
        return run_result(0, stdout="550e8400-e29b-41d4-a716-446655440000\n")

    monkeypatch.setattr("crunchytask.client.subprocess.run", fake_run)

    client = TaskQueueClient(taskq_bin="/usr/bin/taskq")
    client.enqueue(
        "add",
        {"a": 1},
        retry_policy={"max_retries": 0, "base_delay_ms": 1000, "multiplier": 2.0},
    )

    assert calls[0][-2:] == [
        "--retry-policy",
        '{"max_retries":0,"base_delay_ms":1000,"multiplier":2.0}',
    ]


def test_enqueue_rejects_unknown_queue() -> None:
    client = TaskQueueClient(taskq_bin="/usr/bin/taskq")
    with pytest.raises(ValueError, match="unsupported queue"):
        client.enqueue("add", {}, queue="high-priority")


def test_status_parses_taskq_json(monkeypatch: pytest.MonkeyPatch) -> None:
    def fake_run(command, **kwargs):
        assert command[-4:] == ["--format", "json", "--redis", "tcp://127.0.0.1:6379"]
        return run_result(
            0,
            stdout=json.dumps(
                {
                    "task_id": "550e8400-e29b-41d4-a716-446655440000",
                    "status": "running",
                    "failure_reason": None,
                    "result": None,
                }
            )
            + "\n",
        )

    monkeypatch.setattr("crunchytask.client.subprocess.run", fake_run)

    client = TaskQueueClient(taskq_bin="/usr/bin/taskq")
    assert client.status("550e8400-e29b-41d4-a716-446655440000") == "running"


def test_status_surfaces_taskq_errors(monkeypatch: pytest.MonkeyPatch) -> None:
    def fake_run(command, **kwargs):
        return run_result(1, stderr="task not found: missing\n")

    monkeypatch.setattr("crunchytask.client.subprocess.run", fake_run)

    client = TaskQueueClient(taskq_bin="/usr/bin/taskq")
    with pytest.raises(TaskQueueError, match="task not found"):
        client.status("550e8400-e29b-41d4-a716-446655440000")


def test_result_parses_taskq_json(monkeypatch: pytest.MonkeyPatch) -> None:
    def fake_run(command, **kwargs):
        return run_result(
            0,
            stdout=json.dumps(CPP_TASK_RESULT_SUCCESS, separators=(",", ":")) + "\n",
        )

    monkeypatch.setattr("crunchytask.client.subprocess.run", fake_run)

    client = TaskQueueClient(taskq_bin="/usr/bin/taskq")
    assert (
        client.result("550e8400-e29b-41d4-a716-446655440000")
        == CPP_TASK_RESULT_SUCCESS
    )


def test_failed_list_parses_taskq_json(monkeypatch: pytest.MonkeyPatch) -> None:
    def fake_run(command, **kwargs):
        assert command[:3] == ["/usr/bin/taskq", "failed", "list"]
        return run_result(
            0,
            stdout=json.dumps([CPP_IMMEDIATE_MESSAGE], separators=(",", ":")) + "\n",
        )

    monkeypatch.setattr("crunchytask.client.subprocess.run", fake_run)

    client = TaskQueueClient(taskq_bin="/usr/bin/taskq")
    assert client.failed_list() == [CPP_IMMEDIATE_MESSAGE]

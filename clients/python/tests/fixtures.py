"""Golden fixtures derived from the C++ task_json tests."""

CPP_SAMPLE_MESSAGE = {
    "schema_version": 1,
    "id": "550e8400-e29b-41d4-a716-446655440000",
    "name": "add",
    "payload": {"a": 2, "b": 3},
    "status": "pending",
    "retry_count": 0,
    "retry_policy": {
        "max_retries": 3,
        "base_delay_ms": 1000,
        "multiplier": 2.0,
    },
    "created_at_ms": 1717000000000,
    "run_at_ms": 1717000001000,
}

CPP_IMMEDIATE_MESSAGE = {
    "schema_version": 1,
    "id": "550e8400-e29b-41d4-a716-446655440000",
    "name": "add",
    "payload": {"a": 2, "b": 3},
    "status": "pending",
    "retry_count": 0,
    "retry_policy": {
        "max_retries": 3,
        "base_delay_ms": 1000,
        "multiplier": 2.0,
    },
    "created_at_ms": 1717000000000,
}

CPP_TASK_RESULT_SUCCESS = {
    "success": True,
    "payload": {"result": 5},
    "error_message": "",
}

CPP_TASK_RESULT_FAILURE = {
    "success": False,
    "payload": {},
    "error_message": "boom",
}

add_executable(taskqueue_tests
  fake_broker.cc
  broker_test.cc
  broker_stats_test.cc
  dead_letter_test.cc
  crash_recovery_test.cc
  delayed_task_test.cc
  redis_broker_integration_test.cc
  task_json_test.cc
  retry_policy_test.cc
  scheduler_test.cc
  failure_modes_test.cc
  thread_pool_test.cc
  version_test.cc
  worker_test.cc
)

target_include_directories(taskqueue_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
  taskqueue_tests
  PRIVATE
    taskqueue::taskqueue
    Catch2::Catch2WithMain
)

target_compile_definitions(
  taskqueue_tests
  PRIVATE
    TASKQUEUE_ENABLE_INTEGRATION_TESTS=$<BOOL:${TASKQUEUE_ENABLE_INTEGRATION_TESTS}>
)

taskqueue_apply_warnings(taskqueue_tests)

catch_discover_tests(taskqueue_tests)

add_executable(taskqueue_tests version_test.cc)

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

add_executable(taskqueue_bench
  bench_main.cc
  bench_scenarios.cc
)

target_include_directories(taskqueue_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
  taskqueue_bench
  PRIVATE
    taskqueue::taskqueue
    CLI11::CLI11
    nlohmann_json::nlohmann_json
)

taskqueue_apply_warnings(taskqueue_bench)

if(TASKQUEUE_ENABLE_REDIS)
  target_compile_definitions(taskqueue_bench PRIVATE TASKQUEUE_HAS_REDIS=1)
endif()

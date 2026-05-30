add_library(
  taskqueue
  task_id.cc
  task_json.cc
  task_message.cc
  task_result.cc
  task_status.cc
  task_registry.cc
  retry_policy.cc
  runtime_config.cc
  worker_heartbeat.cc
  metrics.cc
  metrics_snapshot.cc
  clock.cc
  scheduler.cc
  thread_pool.cc
  worker.cc
  version.cc
)

if(TASKQUEUE_ENABLE_REDIS)
  target_sources(taskqueue PRIVATE redis_broker.cc redis_scripts.cc)
  target_compile_options(
    taskqueue
    PUBLIC
      "$<$<COMPILE_LANGUAGE:CXX>:-I${CMAKE_BINARY_DIR}/hiredis-include>"
  )
endif()

add_library(taskqueue::taskqueue ALIAS taskqueue)

target_include_directories(
  taskqueue
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(taskqueue PUBLIC cxx_std_20)
taskqueue_apply_warnings(taskqueue)

target_link_libraries(
  taskqueue
  PUBLIC
    $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>
    $<BUILD_INTERFACE:spdlog::spdlog>
)

if(TASKQUEUE_ENABLE_REDIS)
  target_link_libraries(
    taskqueue
    PUBLIC
      $<BUILD_INTERFACE:redis++::redis++_static>
  )
  target_compile_definitions(taskqueue PUBLIC TASKQUEUE_HAS_REDIS=1)
endif()

target_compile_definitions(
  taskqueue
  PUBLIC
    TASKQUEUE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    TASKQUEUE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    TASKQUEUE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(TASKQUEUE_ENABLE_INSTALL)
  install(
    TARGETS taskqueue
    EXPORT taskqueueTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT taskqueue
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT taskqueue
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT taskqueue
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT taskqueue
  )
endif()

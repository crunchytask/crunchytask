add_library(
  taskqueue
  version.cc
)

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
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

if(TASKQUEUE_ENABLE_REDIS)
  target_link_libraries(taskqueue PUBLIC redis++::redis++_static)
  target_compile_definitions(taskqueue PUBLIC TASKQUEUE_HAS_REDIS=1)
endif()

target_compile_definitions(
  taskqueue
  PUBLIC
    TASKQUEUE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    TASKQUEUE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    TASKQUEUE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

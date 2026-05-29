cmake_minimum_required(VERSION 3.24)

# Older FetchContent deps (hiredis, redis-plus-plus) declare legacy minimums.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

project(
  taskqueue
  VERSION 0.1.0
  DESCRIPTION "Distributed task queue runtime in modern C++"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(CMakeDependentOption)
include(FetchContent)
include(GNUInstallDirs)

option(TASKQUEUE_BUILD_CLI "Build the taskq CLI" ON)
option(TASKQUEUE_BUILD_EXAMPLES "Build example programs" ON)
option(TASKQUEUE_BUILD_TESTS "Build unit and integration tests" ON)
option(TASKQUEUE_BUILD_BENCHMARKS "Build operational benchmark executable" OFF)
option(TASKQUEUE_ENABLE_REDIS "Enable Redis broker dependencies" ON)
option(TASKQUEUE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

cmake_dependent_option(
  TASKQUEUE_ENABLE_INTEGRATION_TESTS
  "Run tests that require a live Redis instance"
  ON
  "TASKQUEUE_BUILD_TESTS;TASKQUEUE_ENABLE_REDIS"
  OFF
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
    "In-source builds are not supported. Use:\n"
    "  cmake -S . -B build\n"
    "  cmake --build build"
  )
endif()

function(taskqueue_apply_warnings target)
  target_compile_options(
    ${target}
    PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
  )

  if(TASKQUEUE_WARNINGS_AS_ERRORS)
    target_compile_options(
      ${target}
      PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>
    )
  endif()
endfunction()

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.14.1
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.4.2
  GIT_SHALLOW TRUE
)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json spdlog CLI11)

if(TASKQUEUE_ENABLE_REDIS)
  FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG v1.3.0
    GIT_SHALLOW TRUE
    OVERRIDE_FIND_PACKAGE
  )

  set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(DISABLE_TESTS ON CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(hiredis)

  set(TASKQUEUE_HIREDIS_INCLUDE_ROOT "${CMAKE_BINARY_DIR}/hiredis-include")
  file(MAKE_DIRECTORY "${TASKQUEUE_HIREDIS_INCLUDE_ROOT}")
  file(CREATE_LINK "${hiredis_SOURCE_DIR}" "${TASKQUEUE_HIREDIS_INCLUDE_ROOT}/hiredis" SYMBOLIC)

  FetchContent_Declare(
    redis-plus-plus
    GIT_REPOSITORY https://github.com/sewenew/redis-plus-plus.git
    GIT_TAG 1.3.9
    GIT_SHALLOW TRUE
    PATCH_COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_SOURCE_DIR}/cmake/patches/redis-plus-plus-fetchcontent.patch
      ${CMAKE_BINARY_DIR}/redis-plus-plus.patch
      && bash -c "patch -p1 -N -i ${CMAKE_BINARY_DIR}/redis-plus-plus.patch || true"
  )

  set(REDIS_PLUS_PLUS_BUILD_TEST OFF CACHE BOOL "" FORCE)
  set(REDIS_PLUS_PLUS_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(REDIS_PLUS_PLUS_BUILD_SHARED OFF CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(redis-plus-plus)

  target_compile_options(
    redis++_static
    PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:-I${TASKQUEUE_HIREDIS_INCLUDE_ROOT}>"
      "$<$<COMPILE_LANGUAGE:CXX>:-Wno-error=maybe-uninitialized>"
  )
endif()

if(TASKQUEUE_BUILD_TESTS)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
    GIT_SHALLOW TRUE
  )

  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
  include(CTest)
  include(Catch)
endif()

add_subdirectory(src)

if(TASKQUEUE_BUILD_CLI)
  add_subdirectory(cli)
endif()

if(TASKQUEUE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(TASKQUEUE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(TASKQUEUE_BUILD_TESTS)
  add_subdirectory(tests)

  add_custom_target(
    check
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
    DEPENDS taskqueue_tests
    USES_TERMINAL
    COMMENT "Run all tests (integration tests skip if Redis is unavailable)"
  )
endif()

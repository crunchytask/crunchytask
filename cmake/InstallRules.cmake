include(CMakePackageConfigHelpers)

set(TASKQUEUE_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/taskqueue")

if(TASKQUEUE_BUILD_CLI)
  install(
    TARGETS taskq
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT taskqueue
  )
endif()

install(
  DIRECTORY "${CMAKE_SOURCE_DIR}/include/taskqueue"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  COMPONENT taskqueue
  FILES_MATCHING
  PATTERN "*.h"
)

install(
  EXPORT taskqueueTargets
  NAMESPACE taskqueue::
  DESTINATION "${TASKQUEUE_CMAKE_INSTALL_DIR}"
  FILE taskqueueTargets.cmake
  COMPONENT taskqueue
)

configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/taskqueue-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/taskqueue-config.cmake"
  INSTALL_DESTINATION "${TASKQUEUE_CMAKE_INSTALL_DIR}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/taskqueue-config-version.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/taskqueue-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/taskqueue-config-version.cmake"
  DESTINATION "${TASKQUEUE_CMAKE_INSTALL_DIR}"
  COMPONENT taskqueue
)

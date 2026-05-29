add_executable(taskq main.cc)

target_link_libraries(
  taskq
  PRIVATE
    taskqueue::taskqueue
    CLI11::CLI11
)

taskqueue_apply_warnings(taskq)

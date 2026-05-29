add_executable(producer producer.cc)

target_link_libraries(
  producer
  PRIVATE
    taskqueue::taskqueue
)

taskqueue_apply_warnings(producer)

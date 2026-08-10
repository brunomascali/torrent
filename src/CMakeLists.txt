add_subdirectory(modules/error)
add_subdirectory(modules/io)

add_library(${PROJECT_NAME} INTERFACE)

target_link_libraries(${PROJECT_NAME} INTERFACE
  error
  io
)

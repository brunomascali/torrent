add_subdirectory(modules/error)
add_subdirectory(modules/io)
add_subdirectory(modules/bencode)
add_subdirectory(modules/metainfo)
add_subdirectory(modules/download)
add_subdirectory(modules/logger)
add_subdirectory(modules/crypto)

add_library(${PROJECT_NAME} INTERFACE)

target_link_libraries(${PROJECT_NAME} INTERFACE
  error
  io
  bencode
  download
  logger
  crypto
)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE ${PROJECT_NAME})

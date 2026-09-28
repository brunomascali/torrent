set(TARGET_NAME logger)

add_library(${TARGET_NAME} STATIC
  logger.cxx
  file_sink.cxx
)

target_include_directories(${TARGET_NAME} PUBLIC
  ${CMAKE_SOURCE_DIR}/src/modules
)

target_compile_features(${TARGET_NAME} PUBLIC cxx_std_23)

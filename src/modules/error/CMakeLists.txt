add_library(error INTERFACE)

target_include_directories(error INTERFACE
    ${CMAKE_SOURCE_DIR}/src/modules
)

target_compile_features(error INTERFACE cxx_std_23)

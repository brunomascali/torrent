add_library(io STATIC
    file_handle.cxx
)

target_include_directories(io PUBLIC
    ${CMAKE_SOURCE_DIR}/src/modules
)

target_compile_features(io PUBLIC cxx_std_23)

target_link_libraries(io PRIVATE
    error
)

add_library(bencode STATIC
    decode.cxx
    encode.cxx
)

target_include_directories(bencode PUBLIC
    ${CMAKE_SOURCE_DIR}/src/modules
)

target_compile_features(bencode PUBLIC cxx_std_23)

target_link_libraries(bencode PRIVATE
    error
)

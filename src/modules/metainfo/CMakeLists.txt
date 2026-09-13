set(TARGET_NAME metainfo)

add_library(${TARGET_NAME} STATIC
    metainfo.cxx
)

target_include_directories(${TARGET_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}/src/modules
    /usr/include/boost
)

target_compile_features(${TARGET_NAME} PUBLIC cxx_std_23)

target_link_libraries(${TARGET_NAME} PRIVATE
    error
)

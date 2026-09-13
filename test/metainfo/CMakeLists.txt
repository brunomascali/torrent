set(TARGET_NAME metainfo_test)

find_package(GTest REQUIRED)

add_executable(${TARGET_NAME}
    single_file.cxx
    multiple_file.cxx
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
    GTest::gtest_main
    io
    metainfo
    bencode
)

include(GoogleTest)
gtest_discover_tests(${TARGET_NAME})

find_package(GTest REQUIRED)

add_executable(io_test
    file_handle.cxx
)

target_link_libraries(io_test
    PRIVATE
    GTest::gtest_main
    Torrent
)

include(GoogleTest)
gtest_discover_tests(io_test)

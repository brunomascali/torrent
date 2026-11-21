find_package(GTest REQUIRED)

add_executable(download_test
    download.cxx
)

target_link_libraries(download_test
    PRIVATE
    GTest::gtest_main
    Torrent
)

include(GoogleTest)
gtest_discover_tests(download_test)

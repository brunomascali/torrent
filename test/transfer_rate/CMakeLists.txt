find_package(GTest REQUIRED)

add_executable(transfer_rate_test
    transfer_rate.cxx
)

target_link_libraries(transfer_rate_test
    PRIVATE
    GTest::gtest_main
    Torrent
)

include(GoogleTest)
gtest_discover_tests(transfer_rate_test)

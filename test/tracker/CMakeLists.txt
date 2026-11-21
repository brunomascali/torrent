find_package(GTest REQUIRED)

add_executable(tracker_test
    announce.cxx
    tracker_client.cxx
)

target_link_libraries(tracker_test
    PRIVATE
    GTest::gtest_main
    Torrent
)

include(GoogleTest)
gtest_discover_tests(tracker_test)

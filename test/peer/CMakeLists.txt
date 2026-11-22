find_package(GTest REQUIRED)

add_executable(peer_test
    peer_connection.cxx
    listener.cxx
)

target_link_libraries(peer_test
    PRIVATE
    GTest::gtest_main
    Torrent
)

include(GoogleTest)
gtest_discover_tests(peer_test)

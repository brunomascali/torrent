find_package(GTest REQUIRED)

add_executable(wire_test
    message.cxx
    handshake.cxx
    stream.cxx
)

target_link_libraries(wire_test
    PRIVATE
    GTest::gtest_main
    Torrent
)

include(GoogleTest)
gtest_discover_tests(wire_test)

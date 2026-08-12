find_package(GTest REQUIRED)

add_executable(bencode_test
    bencode.cxx
)

target_link_libraries(bencode_test
    PRIVATE
    GTest::gtest_main
    Torrent
)

include(GoogleTest)
gtest_discover_tests(bencode_test)

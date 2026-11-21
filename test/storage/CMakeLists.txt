find_package(GTest REQUIRED)

add_executable(storage_test
    storage.cxx
)

target_link_libraries(storage_test
    PRIVATE
    GTest::gtest_main
    Torrent
)

include(GoogleTest)
gtest_discover_tests(storage_test)

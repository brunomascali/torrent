find_package(GTest REQUIRED)

add_executable(logger_test
    logger.cxx
)

target_link_libraries(logger_test
    PRIVATE
    GTest::gtest_main
    Torrent
)

include(GoogleTest)
gtest_discover_tests(logger_test)

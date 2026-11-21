find_package(GTest REQUIRED)

add_executable(piece_picker_test
    piece_picker.cxx
)

target_link_libraries(piece_picker_test
    PRIVATE
    GTest::gtest_main
    Torrent
)

include(GoogleTest)
gtest_discover_tests(piece_picker_test)

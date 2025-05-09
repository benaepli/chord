find_package(GTest CONFIG REQUIRED)

add_executable(chord_tests src/basic.cpp)
target_link_libraries(chord_tests PRIVATE chord GTest::gtest GTest::gtest_main)

gtest_discover_tests(chord_tests)
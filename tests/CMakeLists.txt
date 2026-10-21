add_executable(mb_tests test_main.cpp)
target_link_libraries(mb_tests PRIVATE mb_game nlohmann_json::nlohmann_json)
add_test(NAME mb_tests COMMAND mb_tests)

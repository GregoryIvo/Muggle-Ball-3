add_executable(mb_tests test_main.cpp)
target_link_libraries(mb_tests PRIVATE mb_game nlohmann_json::nlohmann_json)
add_test(NAME mb_tests COMMAND mb_tests)

add_executable(mb_render_test render_test.cpp)
target_link_libraries(mb_render_test PRIVATE mb_engine)
target_compile_definitions(mb_render_test PRIVATE "MB3_TEST_OUTPUT_DIR=\"${CMAKE_BINARY_DIR}/test-output\"")
add_test(NAME mb_render_test COMMAND mb_render_test)
# Exit code 77 means no GPU device was available (e.g. CI without one); CTest reports it as skipped.
set_tests_properties(mb_render_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(mb_asset_tests test_assets.cpp)
target_link_libraries(mb_asset_tests PRIVATE mb_game nlohmann_json::nlohmann_json)
add_test(NAME mb_asset_tests COMMAND mb_asset_tests)

# Loads the converted game data when MB3_ASSETS points at it; exits 77 (skipped) otherwise.
add_executable(mb_asset_smoke asset_smoke.cpp)
target_link_libraries(mb_asset_smoke PRIVATE mb_game)
add_test(NAME mb_asset_smoke COMMAND mb_asset_smoke)
set_tests_properties(mb_asset_smoke PROPERTIES SKIP_RETURN_CODE 77)

add_executable(mb_tests test_main.cpp)
target_link_libraries(mb_tests PRIVATE mb_game nlohmann_json::nlohmann_json)
add_test(NAME mb_tests COMMAND mb_tests)

add_executable(mb_scene_tests test_scene.cpp)
target_link_libraries(mb_scene_tests PRIVATE mb_game)
add_test(NAME mb_scene_tests COMMAND mb_scene_tests)

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

# Describes all converted levels against the real data when MB3_ASSETS points at it; exits 77 (skipped) otherwise.
add_executable(mb_scene_smoke scene_smoke.cpp)
target_link_libraries(mb_scene_smoke PRIVATE mb_game)
add_test(NAME mb_scene_smoke COMMAND mb_scene_smoke)
set_tests_properties(mb_scene_smoke PROPERTIES SKIP_RETURN_CODE 77)

# The 2D UI foundation: layout, sprite sheets, string table, bitmap fonts and the form model. Needs no GPU or data.
add_executable(mb_ui_tests test_ui.cpp)
target_link_libraries(mb_ui_tests PRIVATE mb_game nlohmann_json::nlohmann_json)
add_test(NAME mb_ui_tests COMMAND mb_ui_tests)

# Loads every converted form, font and string against the real game data when MB3_ASSETS points at it (and draws
# them when there is a GPU); exits 77 (skipped) otherwise.
add_executable(mb_ui_smoke ui_smoke.cpp)
target_link_libraries(mb_ui_smoke PRIVATE mb_game)
add_test(NAME mb_ui_smoke COMMAND mb_ui_smoke)
set_tests_properties(mb_ui_smoke PROPERTIES SKIP_RETURN_CODE 77)

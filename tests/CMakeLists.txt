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

# The menu runtime and the game flow, driven by synthetic pointer events and keys. Needs no GPU or data.
add_executable(mb_menu_tests test_menu.cpp)
target_link_libraries(mb_menu_tests PRIVATE mb_game nlohmann_json::nlohmann_json)
add_test(NAME mb_menu_tests COMMAND mb_menu_tests)

# The menus and the HUD against the real game data when MB3_ASSETS points at it (and with a GPU, their sprites); exits 77 (skipped)
# otherwise.
add_executable(mb_menu_smoke menu_smoke.cpp)
target_link_libraries(mb_menu_smoke PRIVATE mb_game)
add_test(NAME mb_menu_smoke COMMAND mb_menu_smoke)
set_tests_properties(mb_menu_smoke PROPERTIES SKIP_RETURN_CODE 77)

# The whole front end in a real window, driven by events pushed from a second thread, with the real game data. Exits 77
# (skipped) without MB3_ASSETS, a window system or a GPU.
add_executable(mb_frontend_smoke frontend_smoke.cpp ${PROJECT_SOURCE_DIR}/app/FrontEnd.cpp)
target_include_directories(mb_frontend_smoke PRIVATE ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(mb_frontend_smoke PRIVATE mb_game)
add_test(NAME mb_frontend_smoke COMMAND mb_frontend_smoke)
set_tests_properties(mb_frontend_smoke PROPERTIES SKIP_RETURN_CODE 77)

# Rigid bodies over Jolt: conventions, materials, mass, joints, contacts. CPU only.
add_executable(mb_physics_test physics_test.cpp)
target_link_libraries(mb_physics_test PRIVATE mb_engine)
add_test(NAME mb_physics_test COMMAND mb_physics_test)

# Level -> physics mapping and levels running in a PhysicsScene, with made-up levels. CPU only.
add_executable(mb_physics_scene_tests test_physics_scene.cpp)
target_link_libraries(mb_physics_scene_tests PRIVATE mb_game)
add_test(NAME mb_physics_scene_tests COMMAND mb_physics_scene_tests)

# Runs all converted levels in the physics world when MB3_ASSETS points at the data; exits 77 (skipped) otherwise.
add_executable(mb_physics_smoke physics_smoke.cpp)
target_link_libraries(mb_physics_smoke PRIVATE mb_game)
add_test(NAME mb_physics_smoke COMMAND mb_physics_smoke)
set_tests_properties(mb_physics_smoke PROPERTIES SKIP_RETURN_CODE 77)

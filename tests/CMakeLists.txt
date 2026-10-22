find_package(Threads REQUIRED)

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

# The whole front end in a real window, driven by events pushed from a second thread, with the real game data, the
# game's sound on SDL's disk audio driver, and the player's files in a temporary folder (MB3_USER_DATA). Exits 77
# (skipped) without MB3_ASSETS, a window system or a GPU.
add_executable(mb_frontend_smoke frontend_smoke.cpp ${PROJECT_SOURCE_DIR}/app/FrontEnd.cpp
               ${PROJECT_SOURCE_DIR}/app/FrontEndAudio.cpp ${PROJECT_SOURCE_DIR}/app/GameAudio.cpp)
target_include_directories(mb_frontend_smoke PRIVATE ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(mb_frontend_smoke PRIVATE mb_game)
add_test(NAME mb_frontend_smoke COMMAND mb_frontend_smoke)
set_tests_properties(mb_frontend_smoke PROPERTIES SKIP_RETURN_CODE 77)

# The front end's sound (the game's GameAudio and connectAudio) on SDL's disk audio driver over a made-up sound list: the
# menus' sounds, the music, the volumes and the pause. No game data, window or speakers.
add_executable(mb_wiring_tests test_wiring.cpp ${PROJECT_SOURCE_DIR}/app/FrontEndAudio.cpp ${PROJECT_SOURCE_DIR}/app/GameAudio.cpp)
target_include_directories(mb_wiring_tests PRIVATE ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(mb_wiring_tests PRIVATE mb_game)
target_compile_definitions(mb_wiring_tests PRIVATE "MB3_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")
add_test(NAME mb_wiring_tests COMMAND mb_wiring_tests)

# File helpers and save data, in a temporary folder.
add_executable(mb_save_tests test_save.cpp)
target_link_libraries(mb_save_tests PRIVATE mb_game)
add_test(NAME mb_save_tests COMMAND mb_save_tests)

# Fullscreen shortcuts and the remembered window size, on SDL's dummy video driver; 77 (skipped) if that cannot make a window.
add_executable(mb_window_tests test_window.cpp)
target_link_libraries(mb_window_tests PRIVATE mb_engine)
add_test(NAME mb_window_tests COMMAND mb_window_tests)
set_tests_properties(mb_window_tests PROPERTIES SKIP_RETURN_CODE 77)

# Mixer maths on synthetic PCM, rendered offline: no audio device, no game data.
add_executable(mb_mixer_tests test_mixer.cpp)
target_link_libraries(mb_mixer_tests PRIVATE mb_engine Threads::Threads)
add_test(NAME mb_mixer_tests COMMAND mb_mixer_tests)

# SoundLibrary against a recording stand-in for the audio engine.
add_executable(mb_sound_tests test_sound.cpp)
target_link_libraries(mb_sound_tests PRIVATE mb_game)
add_test(NAME mb_sound_tests COMMAND mb_sound_tests)

# Ogg Vorbis decoding and the audio device (SDL's disk driver) against a synthetic sine wave.
add_executable(mb_audio_tests test_audio.cpp)
target_link_libraries(mb_audio_tests PRIVATE mb_engine)
target_compile_definitions(mb_audio_tests PRIVATE "MB3_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")
add_test(NAME mb_audio_tests COMMAND mb_audio_tests ${CMAKE_BINARY_DIR}/test-output/audio)

# The converted sounds and music from $MB3_ASSETS decode, loop and come out of the audio device
# undistorted; exits 77 (skipped) otherwise.
add_executable(mb_audio_smoke audio_smoke.cpp)
target_link_libraries(mb_audio_smoke PRIVATE mb_game)
add_test(NAME mb_audio_smoke COMMAND mb_audio_smoke ${CMAKE_BINARY_DIR}/test-output/audio-smoke)
set_tests_properties(mb_audio_smoke PROPERTIES SKIP_RETURN_CODE 77)

# Rigid bodies over Jolt: conventions, materials, mass, joints, contacts. CPU only.
add_executable(mb_physics_test physics_test.cpp)
target_link_libraries(mb_physics_test PRIVATE mb_engine)
add_test(NAME mb_physics_test COMMAND mb_physics_test)

# Paths: the curve through a path's points and the actors bound to it, on made-up paths. CPU only.
add_executable(mb_path_tests test_paths.cpp)
target_link_libraries(mb_path_tests PRIVATE mb_game)
add_test(NAME mb_path_tests COMMAND mb_path_tests)

# Level -> physics mapping and levels running in a PhysicsScene, with made-up levels. CPU only.
add_executable(mb_physics_scene_tests test_physics_scene.cpp)
target_link_libraries(mb_physics_scene_tests PRIVATE mb_game)
add_test(NAME mb_physics_scene_tests COMMAND mb_physics_scene_tests)

# Runs all converted levels in the physics world when MB3_ASSETS points at the data; exits 77 (skipped) otherwise.
add_executable(mb_physics_smoke physics_smoke.cpp)
target_link_libraries(mb_physics_smoke PRIVATE mb_game)
add_test(NAME mb_physics_smoke COMMAND mb_physics_smoke)
set_tests_properties(mb_physics_smoke PROPERTIES SKIP_RETURN_CODE 77)

# The path actors of all converted levels running in the physics world when MB3_ASSETS points at the data: on their paths, at
# their tracks' speeds; exits 77 (skipped) otherwise.
add_executable(mb_path_smoke path_smoke.cpp)
target_link_libraries(mb_path_smoke PRIVATE mb_game)
add_test(NAME mb_path_smoke COMMAND mb_path_smoke)
set_tests_properties(mb_path_smoke PROPERTIES SKIP_RETURN_CODE 77)

# Plays all converted levels with the autopilot when MB3_ASSETS points at the data; exits 77 (skipped) otherwise.
add_executable(mb_gameplay_smoke gameplay_smoke.cpp)
target_link_libraries(mb_gameplay_smoke PRIVATE mb_game)
add_test(NAME mb_gameplay_smoke COMMAND mb_gameplay_smoke)
set_tests_properties(mb_gameplay_smoke PROPERTIES SKIP_RETURN_CODE 77)

# The rules of play on made-up levels: bat, ball, hits, explosions, hearts, the end of a level. CPU only.
add_executable(mb_gameplay_tests test_gameplay.cpp)
target_link_libraries(mb_gameplay_tests PRIVATE mb_game)
add_test(NAME mb_gameplay_tests COMMAND mb_gameplay_tests)

# A level of the real game data played by the game's PlaySession on the GPU without a window: the scene (bricks hidden and
# damaged), the bat and the ball drawn, the fades, a level won by the autopilot. Exits 77 (skipped) without MB3_ASSETS or a GPU.
add_executable(mb_play_smoke play_smoke.cpp ${PROJECT_SOURCE_DIR}/app/PlaySession.cpp ${PROJECT_SOURCE_DIR}/app/PlayerInput.cpp
               ${PROJECT_SOURCE_DIR}/app/GameAudio.cpp)
target_include_directories(mb_play_smoke PRIVATE ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(mb_play_smoke PRIVATE mb_game)
add_test(NAME mb_play_smoke COMMAND mb_play_smoke)
set_tests_properties(mb_play_smoke PROPERTIES SKIP_RETURN_CODE 77)

# The mouse and Space as a player's input: the cursor as the bat's target, the click as the launch. CPU only.
add_executable(mb_player_input_tests test_player_input.cpp ${PROJECT_SOURCE_DIR}/app/PlayerInput.cpp)
target_include_directories(mb_player_input_tests PRIVATE ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(mb_player_input_tests PRIVATE mb_game)
add_test(NAME mb_player_input_tests COMMAND mb_player_input_tests)

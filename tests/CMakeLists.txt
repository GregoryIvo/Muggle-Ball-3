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

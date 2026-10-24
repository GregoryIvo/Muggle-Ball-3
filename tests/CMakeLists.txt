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

# The Vita's buttons, stick and touch turned into InputEvents (docs/VITA_PLAN.md 5.8), driven with no window,
# SDL events or device: platform-neutral on purpose, so these rules are covered on the host. CPU only.
add_executable(mb_console_input_tests test_console_input.cpp)
target_link_libraries(mb_console_input_tests PRIVATE mb_engine)
add_test(NAME mb_console_input_tests COMMAND mb_console_input_tests)

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

# Cloth over Jolt's soft bodies: pins, wind, what it meets. CPU only.
add_executable(mb_physics_cloth_test physics_cloth_test.cpp)
target_link_libraries(mb_physics_cloth_test PRIVATE mb_engine)
add_test(NAME mb_physics_cloth_test COMMAND mb_physics_cloth_test)

# Sails on made-up levels: the plan, the scene, the rules and the mesh. CPU only.
add_executable(mb_cloth_tests test_cloth.cpp)
target_link_libraries(mb_cloth_tests PRIVATE mb_game)
add_test(NAME mb_cloth_tests COMMAND mb_cloth_tests)

# The sails of all converted levels in the physics world when MB3_ASSETS points at the data; exits 77 (skipped) otherwise.
add_executable(mb_cloth_smoke cloth_smoke.cpp)
target_link_libraries(mb_cloth_smoke PRIVATE mb_game)
add_test(NAME mb_cloth_smoke COMMAND mb_cloth_smoke)
set_tests_properties(mb_cloth_smoke PROPERTIES SKIP_RETURN_CODE 77)

# The sails on screen, on the real data and the GPU without a window: a mesh each in its material's texture, drawn with the level they change
# only the place of their particles, they follow the physics and go when the rules destroy them. Exits 77 (skipped) without MB3_ASSETS or a GPU.
add_executable(mb_cloth_view_smoke cloth_view_smoke.cpp)
target_include_directories(mb_cloth_view_smoke PRIVATE ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(mb_cloth_view_smoke PRIVATE mb_game)
add_test(NAME mb_cloth_view_smoke COMMAND mb_cloth_view_smoke)
set_tests_properties(mb_cloth_view_smoke PROPERTIES SKIP_RETURN_CODE 77)

# Paths: the curve through a path's points and the actors bound to it, on made-up paths. CPU only.
add_executable(mb_path_tests test_paths.cpp)
target_link_libraries(mb_path_tests PRIVATE mb_game)
add_test(NAME mb_path_tests COMMAND mb_path_tests)

# Level -> physics mapping and levels running in a PhysicsScene, with made-up levels. CPU only.
add_executable(mb_physics_scene_tests test_physics_scene.cpp)
target_link_libraries(mb_physics_scene_tests PRIVATE mb_game)
add_test(NAME mb_physics_scene_tests COMMAND mb_physics_scene_tests)

# The named physics profiles (desktop, vita) and their per-level overrides. CPU only, no game data needed.
add_executable(mb_physics_profile_tests test_physics_profile.cpp)
target_link_libraries(mb_physics_profile_tests PRIVATE mb_game)
add_test(NAME mb_physics_profile_tests COMMAND mb_physics_profile_tests)

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

# The level's special objects (dynamite, cannons, teleports, fireworks, springs, metal ...) on made-up levels. CPU only.
add_executable(mb_special_object_tests test_special_objects.cpp)
target_link_libraries(mb_special_object_tests PRIVATE mb_game)
add_test(NAME mb_special_object_tests COMMAND mb_special_object_tests)

# The special objects of all converted levels, played by the autopilot and poked, when MB3_ASSETS points at the data; exits 77
# (skipped) otherwise.
add_executable(mb_special_object_smoke special_objects_smoke.cpp)
target_link_libraries(mb_special_object_smoke PRIVATE mb_game)
add_test(NAME mb_special_object_smoke COMMAND mb_special_object_smoke)
set_tests_properties(mb_special_object_smoke PROPERTIES SKIP_RETURN_CODE 77)
# The weapons and the strikes of the prizes (the click that fires, the projectiles, the laser, the Thunderbolt and the Meteorite Rain) on made-up
# levels. CPU only.
add_executable(mb_weapon_tests test_weapons.cpp)
target_link_libraries(mb_weapon_tests PRIVATE mb_game)
add_test(NAME mb_weapon_tests COMMAND mb_weapon_tests)

# The weapons and the strikes of all converted levels, played by the autopilot with the click held down, when MB3_ASSETS points at the data; exits
# 77 (skipped) otherwise.
add_executable(mb_weapon_smoke weapon_smoke.cpp)
target_link_libraries(mb_weapon_smoke PRIVATE mb_game)
add_test(NAME mb_weapon_smoke COMMAND mb_weapon_smoke)
set_tests_properties(mb_weapon_smoke PROPERTIES SKIP_RETURN_CODE 77)

# What the weapons look like: the clock of the barrels' animation, the particle effects the events and the flying projectiles call for, and the
# HUD's shot count, on made-up particle files and levels. CPU only.
add_executable(mb_weapon_view_tests test_weapon_view.cpp)
target_link_libraries(mb_weapon_view_tests PRIVATE mb_game)
add_test(NAME mb_weapon_view_tests COMMAND mb_weapon_view_tests)

# The weapons' picture on the real data and the GPU without a window: the barrels in bat.x's own animation, what flies, the beam, the
# lightning, the meteors and their effects, in three worlds. Exits 77 (skipped) without MB3_ASSETS or a GPU.
add_executable(mb_weapon_view_smoke weapon_view_smoke.cpp)
target_include_directories(mb_weapon_view_smoke PRIVATE ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(mb_weapon_view_smoke PRIVATE mb_game)
add_test(NAME mb_weapon_view_smoke COMMAND mb_weapon_view_smoke)
set_tests_properties(mb_weapon_view_smoke PROPERTIES SKIP_RETURN_CODE 77)

# The prizes: what drops, how it falls and is caught, and what each of the 28 does, on made-up levels. CPU only.
add_executable(mb_prize_tests test_prizes.cpp)
target_link_libraries(mb_prize_tests PRIVATE mb_game)
add_test(NAME mb_prize_tests COMMAND mb_prize_tests)

# What the prizes put on screen, without a GPU or data: the caption and its fade, the icon stack and its numbers, the night's light, and
# the words of --drop-prize, --catch-prize and --hud-state.
add_executable(mb_prize_view_tests test_prize_view.cpp ${PROJECT_SOURCE_DIR}/app/PlaySession.cpp ${PROJECT_SOURCE_DIR}/app/PlayerInput.cpp
               ${PROJECT_SOURCE_DIR}/app/GameAudio.cpp ${PROJECT_SOURCE_DIR}/app/FrontEnd.cpp)
target_include_directories(mb_prize_view_tests PRIVATE ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(mb_prize_view_tests PRIVATE mb_game)
add_test(NAME mb_prize_view_tests COMMAND mb_prize_view_tests)

# The prize table against the real game data when MB3_ASSETS points at it (files, halos, materials, captions, icons, catch scores, the
# mean chance of a drop over the 83 levels); exits 77 (skipped) otherwise.
add_executable(mb_prize_smoke prize_smoke.cpp)
target_link_libraries(mb_prize_smoke PRIVATE mb_game nlohmann_json::nlohmann_json)
add_test(NAME mb_prize_smoke COMMAND mb_prize_smoke)
set_tests_properties(mb_prize_smoke PROPERTIES SKIP_RETURN_CODE 77)

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

# The physics bench's core (docs/VITA_PLAN.md M3b): loadBenchConfig's validation, settingsFor's layering,
# the CSV formatting and appendCsv's header-once behaviour. Compiles app/bench/PhysicsBench.cpp straight in,
# the same way other smokes above compile an app/ source directly, rather than a new library target. CPU
# only, no game data: runLevel and runAll (which need a real Assets and real levels) are exercised by hand,
# see the M3b PR description, not here.
add_executable(mb_bench_tests test_bench.cpp ${PROJECT_SOURCE_DIR}/app/bench/PhysicsBench.cpp)
target_include_directories(mb_bench_tests PRIVATE ${PROJECT_SOURCE_DIR}/app ${PROJECT_SOURCE_DIR}/app/bench)
target_link_libraries(mb_bench_tests PRIVATE mb_game nlohmann_json::nlohmann_json)
add_test(NAME mb_bench_tests COMMAND mb_bench_tests)

# The particle definitions (the parser and what it reports) and the particle simulation, on made-up files with the real numbers: each
# type's behaviour, beams and bolts, the system's handles and the batches. CPU only.
add_executable(mb_particle_tests test_particles.cpp)
target_link_libraries(mb_particle_tests PRIVATE mb_game)
add_test(NAME mb_particle_tests COMMAND mb_particle_tests)

# Every file of Data/Particles against the docs and the data that names them, and each definition run through the system, when
# MB3_ASSETS points at the data; exits 77 (skipped) otherwise.
add_executable(mb_particle_smoke particle_smoke.cpp)
target_link_libraries(mb_particle_smoke PRIVATE mb_game nlohmann_json::nlohmann_json)
add_test(NAME mb_particle_smoke COMMAND mb_particle_smoke)
set_tests_properties(mb_particle_smoke PROPERTIES SKIP_RETURN_CODE 77)

# The particle effects in the game's scene, from the rules' events, on the real data and the GPU without a window: a flicker's fire, blasts,
# the flash of each kind of brick, a tree's leaves, the night, and a firework's rocket with its trail and burst. Exits 77 (skipped) without
# MB3_ASSETS or a GPU.
add_executable(mb_effects_smoke effects_smoke.cpp ${PROJECT_SOURCE_DIR}/app/PlaySession.cpp ${PROJECT_SOURCE_DIR}/app/PlayerInput.cpp
               ${PROJECT_SOURCE_DIR}/app/GameAudio.cpp)
target_include_directories(mb_effects_smoke PRIVATE ${PROJECT_SOURCE_DIR}/app)
target_link_libraries(mb_effects_smoke PRIVATE mb_game)
add_test(NAME mb_effects_smoke COMMAND mb_effects_smoke)
set_tests_properties(mb_effects_smoke PROPERTIES SKIP_RETURN_CODE 77)

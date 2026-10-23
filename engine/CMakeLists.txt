add_library(mb_engine
  src/Log.cpp
  src/Paths.cpp
  src/Files.cpp
  src/Window.cpp
  src/Renderer.cpp
  src/PipelineCache.cpp
  src/RenderFrame.cpp
  src/Image.cpp
  src/Model.cpp
  src/Physics.cpp
  src/PhysicsJoints.cpp
  src/PhysicsCloth.cpp
  src/PhysicsShapes.cpp
  src/StbImpl.cpp
  src/VorbisImpl.cpp
  src/Vorbis.cpp
  src/Mixer.cpp
  src/Audio.cpp
  src/CgltfImpl.cpp)
target_include_directories(mb_engine PUBLIC include PRIVATE shaders/generated)
# Only Paths.cpp sees the build machine's assets folder, so changing it rebuilds one file.
if(MB3_DEV_ASSETS)
  set_property(SOURCE src/Paths.cpp APPEND PROPERTY COMPILE_DEFINITIONS "MB3_DEV_ASSETS_DIR=\"${MB3_ASSETS_DIR}\"")
endif()
target_link_libraries(mb_engine
  PUBLIC SDL3::SDL3
  PRIVATE stb cgltf nlohmann_json::nlohmann_json Jolt)

# Graphics back end, chosen when the program is built. SDLGPU has no Vita back end (SDL GPU is desktop
# and console-only); GL and VITAGL are the two flavours of engine/src/render/gl/ (desktop OpenGL 2.1 through
# SDL, and vitaGL on the console); NONE is a build with no GPU API at all, for which the Renderer constructor
# throws and the render/none stub satisfies the rest of mb_engine's interface.
set(MB3_RENDERER "SDLGPU" CACHE STRING
    "Graphics back end: SDLGPU (the SDL3 GPU API: Metal, Vulkan, Direct3D 12), GL (desktop OpenGL 2.1 through SDL, for testing the GL back end on a Mac), VITAGL (the Vita console; needs MB3_PLATFORM_VITA) or NONE (a stub whose Renderer constructor throws, for builds without a GPU API)")
set_property(CACHE MB3_RENDERER PROPERTY STRINGS SDLGPU GL VITAGL NONE)

if(MB3_RENDERER STREQUAL "SDLGPU")
  set(MB3_RENDERER_SOURCES
    src/render/sdlgpu/Renderer.cpp
    src/render/sdlgpu/PipelineCache.cpp
    src/render/sdlgpu/RenderFrame.cpp)
  set(MB3_RENDERER_DEFINE MB3_RENDERER_SDLGPU=1)
elseif(MB3_RENDERER STREQUAL "GL")
  set(MB3_RENDERER_SOURCES
    src/render/gl/Renderer.cpp
    src/render/gl/ProgramCache.cpp
    src/render/gl/RenderFrame.cpp
    src/render/gl/GlPlatformSdl.cpp)
  set(MB3_RENDERER_DEFINE MB3_RENDERER_GL=1)
elseif(MB3_RENDERER STREQUAL "VITAGL")
  if(NOT MB3_PLATFORM_VITA)
    message(FATAL_ERROR "MB3_RENDERER=VITAGL needs a Vita cross build (MB3_PLATFORM_VITA)")
  endif()
  set(MB3_RENDERER_SOURCES
    src/render/gl/Renderer.cpp
    src/render/gl/ProgramCache.cpp
    src/render/gl/RenderFrame.cpp
    src/render/gl/GlPlatformVita.cpp)
  set(MB3_RENDERER_DEFINE MB3_RENDERER_VITAGL=1)
elseif(MB3_RENDERER STREQUAL "NONE")
  set(MB3_RENDERER_SOURCES src/render/none/Renderer.cpp)
  set(MB3_RENDERER_DEFINE MB3_RENDERER_NONE=1)
else()
  message(FATAL_ERROR "Unknown MB3_RENDERER: ${MB3_RENDERER} (expected SDLGPU, GL, VITAGL or NONE)")
endif()

add_library(mb_engine
  src/Log.cpp
  src/Paths.cpp
  src/Files.cpp
  src/Window.cpp
  src/ConsoleInput.cpp
  src/render/RenderCommon.cpp
  ${MB3_RENDERER_SOURCES}
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
target_link_libraries(mb_engine
  PUBLIC SDL3::SDL3
  PRIVATE stb cgltf nlohmann_json::nlohmann_json Jolt)
target_compile_definitions(mb_engine PUBLIC ${MB3_RENDERER_DEFINE})
if(MB3_RENDERER STREQUAL "GL")
  find_package(OpenGL REQUIRED)
  target_link_libraries(mb_engine PRIVATE OpenGL::GL)
elseif(MB3_RENDERER STREQUAL "VITAGL")
  target_link_libraries(mb_engine PRIVATE mb3_vitagl)
endif()

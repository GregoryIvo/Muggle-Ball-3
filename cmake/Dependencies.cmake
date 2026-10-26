include(FetchContent)

# SDL3: window, input, audio device and the GPU API (Metal on macOS,
# Vulkan / Direct3D 12 elsewhere).
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
# An installed SDL3 is used unless MB3_FETCH_SDL3 asks for the pinned tag, built from source and linked
# statically (scripts/package_macos.sh does, so the app needs nothing from /opt/homebrew).
set(mb3_sdl3_find FIND_PACKAGE_ARGS 3.2 CONFIG)
if(MB3_FETCH_SDL3)
  set(mb3_sdl3_find "")
  message(STATUS "SDL3: building release-3.2.10 from source, linked statically")
endif()
if(MB3_PLATFORM_VITA)
  # The SDK's own SDL3 (the vdpm package), and only that: the toolchain file limits find_package to the SDK,
  # and a fetched copy would be built for the wrong system.
  find_package(SDL3 CONFIG REQUIRED)
  message(STATUS "SDL3 ${SDL3_VERSION} from the Vita SDK (${SDL3_DIR})")
else()
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.10
    GIT_SHALLOW TRUE
    ${mb3_sdl3_find})
endif()

# JSON for the converted level, material and mesh files.
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
  FIND_PACKAGE_ARGS 3.11 CONFIG)

# stb_image / stb_image_write: JPEG and PNG decoding, PNG screenshots.
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d)

# cgltf: reads the .glb meshes written by tools/x2glb.py.
FetchContent_Declare(cgltf
  GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
  GIT_TAG v1.15
  GIT_SHALLOW TRUE)

# Jolt Physics: rigid bodies, standing in for the original's PhysX 2.x.
# OVERRIDE_CXX_FLAGS would rewrite the global compiler flags (no RTTI, no exceptions).
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
# The GPU compute back ends (hair, cloth) are of no use to a rigid body game.
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG v5.6.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR Build)

set(mb3_dependencies nlohmann_json stb cgltf JoltPhysics)
if(NOT MB3_PLATFORM_VITA)
  list(PREPEND mb3_dependencies SDL3)
endif()
FetchContent_MakeAvailable(${mb3_dependencies})

if(MB3_PLATFORM_VITA)
  # Jolt's Core.h knows no Vita and stops with "Unknown platform", but it checks JPH_PLATFORM_BLUE first and
  # then includes <Jolt/Core/PlatformBlue.h>, which the platform provides: ours is in platform/vita/jolt.
  # PUBLIC, so the engine's Physics*.cpp compile Jolt's headers the same way. Newlib has no posix_memalign,
  # which Jolt's allocator calls; mb3_vita_compat supplies it.
  target_compile_definitions(Jolt PUBLIC JPH_PLATFORM_BLUE)
  target_include_directories(Jolt BEFORE PUBLIC ${PROJECT_SOURCE_DIR}/platform/vita/jolt)
  target_link_libraries(Jolt PUBLIC mb3_vita_compat)
endif()

# stb and cgltf are header-only and ship no CMake project of their own.
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
add_library(cgltf INTERFACE)
target_include_directories(cgltf SYSTEM INTERFACE ${cgltf_SOURCE_DIR})

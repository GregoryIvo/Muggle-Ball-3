include(FetchContent)

# SDL3: window, input, audio device and the GPU API (Metal on macOS,
# Vulkan / Direct3D 12 elsewhere).
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.10
  GIT_SHALLOW TRUE
  FIND_PACKAGE_ARGS 3.2 CONFIG)

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

FetchContent_MakeAvailable(SDL3 nlohmann_json stb cgltf)

# stb and cgltf are header-only and ship no CMake project of their own.
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
add_library(cgltf INTERFACE)
target_include_directories(cgltf SYSTEM INTERFACE ${cgltf_SOURCE_DIR})

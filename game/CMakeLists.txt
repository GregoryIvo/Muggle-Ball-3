add_library(mb_game
  src/ObjectType.cpp
  src/Level.cpp
  src/LevelLoader.cpp
  src/TextureIndex.cpp
  src/MaterialLibrary.cpp
  src/SceneBuilder.cpp
  src/ModelCache.cpp
  src/LevelScene.cpp
  src/LevelCatalog.cpp
  src/GameCamera.cpp
  src/SaveData.cpp)
target_include_directories(mb_game PUBLIC include)
target_link_libraries(mb_game PUBLIC mb_engine PRIVATE nlohmann_json::nlohmann_json)

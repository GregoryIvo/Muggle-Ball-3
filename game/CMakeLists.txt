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
  src/UiLayout.cpp
  src/StringTable.cpp
  src/BitmapFont.cpp
  src/UiForm.cpp
  src/UiAssets.cpp
  src/UiDraw.cpp
  src/Profile.cpp
  src/Campaign.cpp
  src/Menu.cpp
  src/Hud.cpp
  src/GameFlow.cpp)
target_include_directories(mb_game PUBLIC include)
target_link_libraries(mb_game PUBLIC mb_engine PRIVATE nlohmann_json::nlohmann_json)

# Copies the converted assets into an app bundle: cmake -DSRC=<assets> -DDST=<Resources/assets> -P BundleAssets.cmake
# The icon is already in Resources (CFBundleIconFile), so it is left out here.
file(COPY "${SRC}/" DESTINATION "${DST}" PATTERN "AppIcon.icns" EXCLUDE)

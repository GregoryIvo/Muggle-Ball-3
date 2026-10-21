#pragma once

#include <string>

namespace mb_app {

// `MuggleBall3 --form <name>`: shows a menu form with nothing else, to check the UI foundation against the
// original's look and to try its states with the mouse.
struct FormViewerOptions {
    std::string form;        // a name from Data/Menu, any case: "MainMenu", "Options", "stagepiratos"
    std::string screenshot;  // write a PNG without opening a window
    int width = 1024;
    int height = 768;
    std::string language;    // a language of the string table; empty keeps the game's default
    std::string hover;       // screenshot: draw the first widget with this name hovered
    std::string press;       // screenshot: ... or pressed
    bool background = true;  // draw the "background" form (sky and stone frame) behind it
};

int runFormViewer(const FormViewerOptions& options);

}  // namespace mb_app

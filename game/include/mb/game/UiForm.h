#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/StringTable.h"
#include "mb/game/UiLayout.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mb {

// What kind of widget an element of a form is. They all share the one UiWidget struct; the kind decides how a
// widget is drawn and which ones the pointer can touch.
enum class WidgetKind {
    Form,      // the root <form>: the form's own settings, and an image if it has one (the sky of "background")
    Label,     // an image and/or text, never interactive. The data's one misspelt <La1bel> is a Label too
    Button,
    Checkbox,  // a sprite sheet of two frames, unchecked and checked
    Slider,    // `image` is the knob; the track and the - and + buttons are its child widgets
    List,      // a choice among strings. Draws nothing itself: its `text` is a "%s" format for the child label that
               // shows the selected item, and its - and + buttons are child widgets
    TextEdit,  // an image and the text typed into it (set `text`)
    Progress,  // an image revealed from the left as `value` rises
};

// Where a widget's text goes relative to its rectangle (the data's text_align). `textOffset` is the gap in
// reference pixels along the direction the text moves away from the rectangle.
enum class TextPlacement {
    Center,           // centred on the rectangle; on its position when the widget has no size
    CenterMultiline,  // wrapped to the rectangle's width, each line centred, from the top edge down
    Right,            // beside the right edge, vertically centred
    Above,            // centred above the top edge
    Under,            // centred below the bottom edge; a negative offset moves it up into the rectangle
    Bottom,           // the same as Under (no shipped form puts text there)
};

// The states a sprite sheet can show a different frame for, in the order the data lists them.
enum class UiState { Normal, Hover, Pressed, Disabled };

// A widget's image: a PNG that is one picture or a grid of equal frames (img_name and img_params).
struct SpriteSheet {
    std::string name;  // as written in the data, e.g. "forms/button.dds"; empty for none. The extension is ignored
    int columns = 1;
    int rows = 1;
    // img_params lists one frame per state after the grid size: "1 3 1 2 2 0" is a 1x3 sheet showing frame 1
    // normally, 2 on hover, 2 pressed and 0 disabled. Checkboxes list two: unchecked, then checked. Fewer entries
    // than states repeat the last one, and none at all means frame 0.
    std::vector<int> frames;
    bool tiled = false;  // repeat the image at its own size across the rectangle instead of stretching it

    bool empty() const { return name.empty(); }
};

// The frame number a sheet shows in a state (the rule described at SpriteSheet::frames).
int sheetFrame(const std::vector<int>& frames, UiState state);

// Where a frame lies in its image. Frames count down each column before moving to the next column (frame =
// column * rows + row), which is how the 2x3 bat sheet reads: 0, 1, 2 are the first column and 3, 4, 5 the second.
struct FrameUv {
    Vec2 uv0;  // top-left corner, as Sprite::uv0
    Vec2 uv1;  // bottom-right corner
};
FrameUv sheetFrameUv(int columns, int rows, int frame);

// One element of a form: its data from the converted JSON, the state the menu code changes while it runs, and
// the pixel rectangles worked out by UiForm::layout. A widget's position in UiForm::widgets() is also its
// drawing order.
struct UiWidget {
    WidgetKind kind = WidgetKind::Label;
    std::string tag;   // the XML tag as written, e.g. "label", "La1bel"
    std::string name;  // the data's name; not unique ("Uzor" appears in many panels) and sometimes a number ("46")
    int parent = -1;   // index of the enclosing widget, -1 for the root form
    int end = 0;       // one past the index of the widget's last descendant: its subtree is [index, end)

    // --- from the data. Lengths are fractions of the 4:3 screen unless noted, y up (see UiLayout) ---
    Vec2 pos;                // bottom-left corner
    Vec2 size;               // zero: the size of one image frame, or no size for a label that is only text
    Vec2 sizePos;            // slider: the size of the knob
    Color color{1, 1, 1, 1};            // multiplies the image
    Color shadowColor{0, 0, 0, 1};      // of the image's and the text's drop shadow
    Vec2 shadowOffset;       // reference pixels, right and down; zero: the image has no shadow
    Vec2 pressOffset;        // reference pixels the image and text move while pressed
    float mouseOverOffset = 0;  // reference pixels the image grows by on every side while hovered
    float rotation = 0;      // degrees about the image's centre, positive clockwise on screen
    SpriteSheet image;
    std::string textKey;     // `text` as written: a key of the string table or literal text
    std::string text;        // what is drawn: textKey resolved when the form was loaded. Menu code may replace it
    TextPlacement textPlacement = TextPlacement::Center;
    float textOffset = 0;    // reference pixels
    std::string fontFace;    // font_name resolved through the string table: "bookworm.ttf"
    int fontSize = 16;       // in the 1024x768 layout
    Color fontColor{1, 1, 1, 1};
    Vec2 fontShadow;         // reference pixels, right and down; zero: the text has no shadow
    // user_data: what the game should do with the widget: "Navigate" (the widget's name is the form to show:
    // UiAssets::loadForm(name)), "SelectLevel" (the name is the level's index, 0 to 82 over the four stages in the order pirates, farm, cowboys, knights), "SelectBat", "ApplyVideo",
    // "ApplyRecord", "OnlineScore" (the name is the period the score request asks for), "FinishGame".
    std::string userData;
    std::string soundMouseIn;
    std::string soundMouseDown;
    bool noFading = false;   // stays fully opaque while the form fades in
    bool cycled = false;     // list: stepping past an end wraps around
    // Slider and progress: params = minimum, maximum and (slider) the step `value` snaps to.
    float rangeMin = 0;
    float rangeMax = 1;
    float step = 0;

    // --- state: set by the menu code, or by UiForm::updatePointer ---
    bool enabled = true;     // data: disabled="no"
    bool visible = true;     // hides the widget and everything inside it
    bool hovered = false;
    bool pressed = false;    // pressed while the pointer is still over it
    bool checked = false;    // checkbox
    float value = 0;         // slider and progress, between rangeMin and rangeMax

    // --- from UiForm::layout, in target pixels ---
    Rect rect;               // at rest, without hover growth or press offset
    Vec2 knobSize;           // slider

    UiState state() const;
    // The sprite-sheet frame to draw now: by state, or by `checked` for a checkbox.
    int frame() const;
    // Whether the pointer can touch it: buttons, checkboxes, sliders and text fields.
    bool interactive() const;
    // value as a fraction of the range, 0 to 1.
    float fraction() const;
    // Slider: where the knob is now. Its left edge runs along `rect`, from `value` = min to max.
    Rect knobRect() const;
    // Slider: the value a pointer at this x (pixels) means, centring the knob on it and snapping to the step.
    float valueAt(float pixelX) const;
    // The area the pointer can touch: `rect`, plus the knob's overhang for a slider.
    Rect hitRect() const;
};

// What changed in one UiForm::updatePointer call. Each field is set only in the call where the thing happened and
// is null otherwise. The pointers point into the form's widgets and stay valid as long as the form lives.
struct PointerEvents {
    UiWidget* entered = nullptr;  // the pointer moved onto it (play its soundMouseIn)
    UiWidget* pressed = nullptr;  // the button went down on it (play its soundMouseDown)
    UiWidget* clicked = nullptr;  // the button was released over the widget it went down on
};

// The size in pixels of a whole image file (not one frame) given the img_name used in a form, or (0, 0) if
// there is no such image.
using ImageSizeFn = std::function<Vec2(std::string_view imageName)>;

// A menu screen: a tree of widgets loaded from Data/Menu/<name>.json (written by tools/ui.py from the game's XML,
// documented in docs/ASSET_FORMATS.md). The model holds the data, the widgets' state and their pixel rectangles
// and does the hit testing; drawing is UiDraw.h, resources are UiAssets.h.
//
//   UiForm form = assets.loadForm("MainMenu");
//   each frame:  form.layout(UiLayout::fit(width, height), assets.imageSizes());
//                PointerEvents events = form.updatePointer(mouse, buttonDown);   // then act on events.clicked
//                drawForm(form, assets, sprites);
//
// A form is movable, not copyable (it remembers which widget the pointer is on).
class UiForm {
public:
    // Throws std::runtime_error on a file that is not a form. Elements the game does not know are skipped with a
    // warning. Texts are resolved through `strings` once, here.
    static UiForm load(const std::filesystem::path& json, const StringTable& strings);
    static UiForm parse(std::string_view json, const StringTable& strings);

    UiForm() = default;
    UiForm(UiForm&&) = default;
    UiForm& operator=(UiForm&&) = default;

    const std::string& name() const { return name_; }
    // The time the form takes to fade in, in milliseconds (the data's fade_time_msec).
    int fadeTimeMs() const { return fadeTimeMs_; }

    // Document order: parents before their children, siblings in file order. This is the drawing order; the
    // pointer reaches the last widget first. Index 0 is the root form. Change widgets' state freely, but do not add
    // or remove widgets: `parent`, `end` and the pointer's bookkeeping are indices.
    std::vector<UiWidget>& widgets() { return widgets_; }
    const std::vector<UiWidget>& widgets() const { return widgets_; }
    // The first widget with this name, or null.
    UiWidget* find(std::string_view name);
    const UiWidget* find(std::string_view name) const;

    // Works out every widget's rectangle in pixels for a target and remembers `screen` for drawing. Call it when
    // the target size changes (it is cheap enough to call every frame). `imageSizes` is only asked about widgets
    // whose size is zero and which have an image: they take the size of one frame of it, in reference pixels.
    void layout(const UiLayout& screen, const ImageSizeFn& imageSizes);
    const UiLayout& screen() const { return screen_; }

    // The widget at a pixel, or null: the last (topmost) interactive, enabled and visible one whose hitRect
    // contains it.
    UiWidget* hitTest(Vec2 pixel);

    // Moves the pointer to `pixel` with the button up or down and updates the widgets' hovered and pressed flags:
    // a widget is hovered while the pointer is on it; it is pressed from the button going down on it until the
    // button comes up, but only while the pointer stays on it.
    PointerEvents updatePointer(Vec2 pixel, bool buttonDown);

private:
    std::string name_;
    int fadeTimeMs_ = 0;
    std::vector<UiWidget> widgets_;
    UiLayout screen_;
    int hovered_ = -1;  // indices into widgets_
    int pressed_ = -1;
    bool buttonDown_ = false;
};

}  // namespace mb

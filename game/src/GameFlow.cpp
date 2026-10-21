#include "mb/game/GameFlow.h"

#include <algorithm>

namespace mb {

namespace {

bool has(const std::vector<UiKey>& keys, UiKey key) {
    return std::ranges::find(keys, key) != keys.end();
}

}  // namespace

GameFlow::Step GameFlow::update(const UiInput& input, double seconds, const UiLayout& layout, const ImageSizeFn& imageSizes) {
    Step step;
    switch (mode_) {
        case Mode::Menu: {
            const MenuResult result = menu_.update(input, seconds, layout, imageSizes);
            if (result.kind == MenuResult::Kind::Quit) {
                step.quit = true;
            } else if (result.kind == MenuResult::Kind::StartLevel) {
                level_ = result.level;
                mode_ = Mode::Loading;
                loadAsked_ = loadDone_ = false;
                menu_.open("Loading");  // drawn for a frame before the (blocking) build starts
            }
            break;
        }
        case Mode::Loading:
            menu_.update({}, seconds, layout, imageSizes);
            if (!loadAsked_) {
                loadAsked_ = true;
                step.loadLevel = level_;
            } else if (loadDone_ && menu_.shownLongEnough()) {
                mode_ = Mode::Playing;
                pointerDown_ = false;
            }
            break;
        case Mode::Playing:
            for (const UiInput::Pointer& sample : input.pointer) {
                pointerDown_ = sample.down;
            }
            if (has(input.keys, UiKey::Escape)) {
                mode_ = Mode::Paused;
            }
            break;
        case Mode::Paused: {
            // Any key but Escape, or a mouse button going down, carries on.
            bool resume = std::ranges::any_of(input.keys, [](UiKey key) { return key != UiKey::Escape; });
            for (const UiInput::Pointer& sample : input.pointer) {
                resume = resume || (sample.down && !pointerDown_);
                pointerDown_ = sample.down;
            }
            if (has(input.keys, UiKey::Escape)) {
                mode_ = Mode::Menu;
                menu_.open("MainMenu");
            } else if (resume) {
                mode_ = Mode::Playing;
            }
            break;
        }
    }
    return step;
}

void GameFlow::loaded(bool ok) {
    if (mode_ != Mode::Loading) {
        return;
    }
    if (ok) {
        loadDone_ = true;
        menu_.setProgress(1);
        hud_ = GameHud{};
        hud_.level = level_ + 1;
    } else {
        mode_ = Mode::Menu;
        const Stage* stage = campaign_.stageOf(level_);
        menu_.open(stage ? stage->form : "MainMenu");
    }
}

}  // namespace mb

#include "mb/game/GameFlow.h"

#include "mb/game/Gameplay.h"

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
                startGame(result.level);
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

void GameFlow::startGame(int level) {
    level_ = level;
    hud_ = GameHud{};
    hud_.lives = GameplayRules{}.startHearts;
    hud_.level = level + 1;
    levelStartScore_ = 0;
    mode_ = Mode::Loading;
    loadAsked_ = loadDone_ = advancing_ = false;
    menu_.open("Loading");  // drawn for a frame before the (blocking) build starts
}

void GameFlow::loaded(bool ok) {
    if (mode_ != Mode::Loading && !advancing_) {
        return;
    }
    if (ok && !advancing_) {
        loadDone_ = true;
        menu_.setProgress(1);
    }
    advancing_ = false;
    if (!ok) {
        mode_ = Mode::Menu;
        const Stage* stage = campaign_.stageOf(level_);
        menu_.open(stage ? stage->form : "MainMenu");
    }
}

GameFlow::Step GameFlow::levelEnded(LevelOutcome outcome) {
    Step step;
    if (mode_ != Mode::Playing || outcome == LevelOutcome::Playing) {
        return step;
    }
    const std::string* name = campaign_.levelName(level_);
    const std::string level = name ? *name : std::string();
    const int score = hud_.score;
    const int earned = score - levelStartScore_;
    if (outcome == LevelOutcome::GameOver) {
        // The level's best is what was made on it, not the total the game was carrying.
        save_.progress.recordScore(level, earned);
        save_.saveProgress();
        mode_ = Mode::Menu;
        menu_.openGameOver(score, level);
        return step;
    }
    if (const Stage* stage = campaign_.stageOf(level_)) {
        save_.progress.recordCompletion(stage->name, level_ - stage->first, level, earned);
        save_.saveProgress();
    }
    if (level_ + 1 < campaign_.levelCount()) {
        ++level_;
        hud_.level = level_ + 1;
        hud_.powerUps.clear();
        levelStartScore_ = score;
        advancing_ = true;
        step.loadLevel = level_;
    } else {
        mode_ = Mode::Menu;
        menu_.openEnding(score, level);
    }
    return step;
}

}  // namespace mb

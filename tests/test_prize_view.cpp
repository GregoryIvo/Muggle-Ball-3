// What the prizes put on screen, without a GPU or game data: the caption and its fade, the icon stack with its numbers, the night's
// light, and the words the harness takes for a prize to drop or catch (--drop-prize, --catch-prize) and for the HUD (--hud-state).

#include "FrontEnd.h"
#include "PlaySession.h"
#include "mb/game/PrizeHud.h"
#include "mb/game/PrizeScene.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mb;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) <= tolerance;
}

GameEvent caught(PrizeKind kind, int points = 100) {
    GameEvent event;
    event.type = EventType::PrizeCaught;
    event.prize = kind;
    event.points = points;
    return event;
}

void testCaptionFade() {
    check(captionAlpha(0) == 1, "caption: strong at once");
    check(captionAlpha(kCaptionStrongSeconds) == 1, "caption: strong for about 1 s");
    check(near(captionAlpha((kCaptionStrongSeconds + kCaptionSeconds) / 2), 0.5f), "caption: half way through the fade, half strong");
    check(captionAlpha(kCaptionSeconds) == 0 && captionAlpha(5) == 0 && captionAlpha(-1) == 0, "caption: gone after about 2.2 s");
    float last = 1;
    bool falling = true;
    for (double age = 0; age < 3; age += 0.05) {
        const float now = captionAlpha(age);
        falling = falling && now <= last;
        last = now;
    }
    check(falling, "caption: it only fades out");
}

void testCaption() {
    PrizeCaption caption;
    check(caption.key().empty() && caption.alpha() == 0, "caption: nothing before a catch");

    const GameEvent bigBall = caught(PrizeKind::BigBall);
    caption.update({&bigBall, 1}, 1.0 / 60);
    check(caption.key() == "BigBall" && caption.alpha() == 1, "caption: the prize's key, at once");
    caption.update({}, 0.9);
    check(caption.key() == "BigBall" && caption.alpha() == 1, "caption: still strong a second after");
    caption.update({}, 0.7);
    check(caption.key() == "BigBall" && caption.alpha() < 1 && caption.alpha() > 0, "caption: fading at 1.6 s");
    caption.update({}, 1.0);
    check(caption.key().empty() && caption.alpha() == 0, "caption: gone after 2.2 s");

    // A catch replaces the caption on show, and starts it over.
    const GameEvent night = caught(PrizeKind::Night), drunken = caught(PrizeKind::Drunken);
    caption.update({&night, 1}, 0.1);
    caption.update({}, 1.5);
    caption.update({&drunken, 1}, 0.1);
    check(caption.key() == "Drunken Ball" && caption.alpha() == 1, "caption: a later catch replaces it and is strong at once");
    // Two catches in one frame: the later one shows.
    const GameEvent both[] = {caught(PrizeKind::Life), caught(PrizeKind::Wall)};
    caption.update(both, 0.1);
    check(caption.key() == "Wall", "caption: of two catches in a frame, the later");

    // The gift of a Surprise (a catch worth nothing) leaves the "Surprise" on show.
    PrizeCaption surprise;
    const GameEvent gifts[] = {caught(PrizeKind::Surprise, 299), caught(PrizeKind::Cannon, 0)};
    surprise.update(gifts, 0.1);
    check(surprise.key() == "Surprise", "caption: the Surprise stays, whatever it gave");

    // Other events change nothing.
    PrizeCaption quiet;
    GameEvent missed;
    missed.type = EventType::PrizeMissed;
    missed.prize = PrizeKind::Bonus;
    GameEvent started;
    started.type = EventType::EffectStarted;
    started.prize = PrizeKind::Night;
    const GameEvent others[] = {missed, started};
    quiet.update(others, 0.1);
    check(quiet.key().empty(), "caption: only a catch shows a caption");
}

void testPowerUps() {
    const std::vector<ActiveEffect> effects = {
        {PrizeKind::BigBall, 19.2, 20, 0},   {PrizeKind::Gun, 0, 0, 12},      {PrizeKind::Night, 9.0, 10, 0},
        {PrizeKind::Quake, 2.0, 3, 0},       {PrizeKind::ExplosiveBall, 0, 0, 1}, {PrizeKind::Magnet, 0.01, 30, 0},
        {PrizeKind::Laser, 0, 0, 5},         {PrizeKind::Wind, 4, 5, 0},      {PrizeKind::YoYo, 7, 7.5, 0},
    };
    const std::vector<PowerUp> icons = powerUpsOf(effects);
    check(icons.size() == 5, "icons: " + std::to_string(icons.size()) + " of 9 effects have an icon (not the Quake, the Explosive Ball, the Wind, the Yo-Yo)");
    if (icons.size() != 5) {
        return;
    }
    check(icons[0].icon == "big" && icons[0].number == 20, "icons: Big Ball shows 20 for 19.2 s (counted up)");
    check(icons[1].icon == "Gun" && icons[1].number == 12, "icons: the Machine Gun shows its shots");
    check(icons[2].icon == "Night" && icons[2].number == 9, "icons: the Night shows 9 for 9.0 s");
    check(icons[3].icon == "Magnit" && icons[3].number == 1, "icons: an effect with a hundredth of a second left shows 1");
    check(icons[4].icon == "Lazer" && icons[4].number == 5, "icons: the Laser shows 5 shots, in the order they were caught (oldest first)");
    check(powerUpsOf({}).empty(), "icons: none with no effects");

    // Every prize that has an icon in the table shows one with a number.
    for (int i = 0; i < kPrizeKindCount; ++i) {
        const PrizeKind kind = static_cast<PrizeKind>(i);
        const PrizeInfo& info = prizeInfo(kind);
        ActiveEffect effect{kind, info.seconds, info.seconds, info.count};
        const std::vector<PowerUp> one = powerUpsOf({&effect, 1});
        check(one.size() == (info.icon.empty() ? 0u : 1u), std::string(info.stem) + ": an icon exactly when the table has one");
        check(one.empty() || (one[0].icon == info.icon && one[0].number > 0), std::string(info.stem) + ": its icon and a number to count down from");
    }
}

void testNight() {
    DirectionalLight day;
    day.diffuse = {0.8f, 0.6f, 0.4f, 1};
    day.ambient = {0.5f, 0.5f, 0.5f, 1};
    const DirectionalLight same = nightLight(day, 0);
    check(near(same.diffuse.r, 0.8f) && near(same.ambient.g, 0.5f), "night: no change by day");
    const DirectionalLight dark = nightLight(day, 1);
    check(near(dark.diffuse.r, 0.8f * 0.38f) && near(dark.diffuse.b, 0.4f * 0.38f) && near(dark.ambient.r, 0.5f * 0.38f) && dark.diffuse.a == 1,
          "night: the board's brightness falls to 0.38 at full night");
    const DirectionalLight dusk = nightLight(day, 0.5f);
    check(near(dusk.diffuse.r, 0.8f * 0.69f), "night: half way, half the fall");
    check(dark.direction.y == day.direction.y, "night: the light's direction is the day's");

    DrawItem lit, unlit;
    lit.material.lit = true;
    unlit.material.lit = false;
    unlit.material.diffuse = {1, 0.5f, 0.25f, 0.8f};
    std::vector<DrawItem> items = {lit, unlit};
    dimUnlit(items, 0);
    check(items[1].material.diffuse.r == 1, "night: no dimming by day");
    dimUnlit(items, 1);
    check(items[0].material.diffuse.r == 1, "night: what the light dims is left to it");
    check(near(items[1].material.diffuse.r, 0.49f) && near(items[1].material.diffuse.g, 0.245f) && items[1].material.diffuse.a == 0.8f,
          "night: the sky and the flashes go to 0.49, their opacity unchanged");
}

void testPrizeCue() {
    mb_app::PrizeCue cue;
    check(mb_app::parsePrizeCue("bazuka", false, cue) && cue.kind == PrizeKind::Bazooka && cue.seconds == 0 && !cue.catchNow && !cue.x,
          "cue: a prize by the stem of its file");
    check(mb_app::parsePrizeCue("Tripple@12.5", true, cue) && cue.kind == PrizeKind::Multiball && cue.seconds == 12.5 && cue.catchNow,
          "cue: any case, with the seconds to wait");
    check(mb_app::parsePrizeCue("wind@3,-8.5", false, cue) && cue.kind == PrizeKind::Wind && cue.seconds == 3 && cue.x && *cue.x == -8.5f,
          "cue: and where it falls from");
    check(mb_app::parsePrizeCue("bonus,4", false, cue) && cue.seconds == 0 && cue.x && *cue.x == 4, "cue: a place and no time");
    for (const char* bad : {"", "nothing", "bonus@", "bonus@x", "bonus@-1", "bonus@1,", "bonus@1,x", "bonus,3@1", "@1", "life@1@2"}) {
        mb_app::PrizeCue before = cue;
        check(!mb_app::parsePrizeCue(bad, false, cue) && cue.kind == before.kind && cue.seconds == before.seconds, std::string("cue: \"") + bad + "\" is refused and changes nothing");
    }
    for (int i = 0; i < kPrizeKindCount; ++i) {
        const PrizeKind kind = static_cast<PrizeKind>(i);
        check(mb_app::parsePrizeCue(std::string(prizeInfo(kind).stem), false, cue) && cue.kind == kind, std::string(prizeInfo(kind).stem) + ": every prize can be named");
    }
}

void testHudState() {
    GameHud hud;
    check(mb_app::parseHudState("5,768915,62,Cannon:6,Magnit:30,Night", hud), "hud-state: icons with and without numbers");
    check(hud.lives == 5 && hud.score == 768915 && hud.level == 62 && hud.powerUps.size() == 3, "hud-state: the numbers of the line, three icons");
    check(hud.powerUps.size() == 3 && hud.powerUps[0].icon == "Cannon" && hud.powerUps[0].number == 6 && hud.powerUps[1].number == 30 &&
              hud.powerUps[2].icon == "Night" && hud.powerUps[2].number < 0,
          "hud-state: each icon with its number, none for the last");
    check(!mb_app::parseHudState("5,768915,62,Cannon:", hud) && !mb_app::parseHudState("5,768915,62,:6", hud) &&
              !mb_app::parseHudState("5,768915,62,Cannon:x", hud) && !mb_app::parseHudState("5,768915,62,Cannon:-1", hud),
          "hud-state: a number that is not one, or an icon with no name, is refused");
    check(mb_app::parseHudState("3,1529,1", hud) && hud.powerUps.empty(), "hud-state: no icons");
}

}  // namespace

int main() {
    testCaptionFade();
    testCaption();
    testPowerUps();
    testNight();
    testPrizeCue();
    testHudState();
    if (failures == 0) {
        std::puts("all prize view tests passed");
    }
    return failures == 0 ? 0 : 1;
}

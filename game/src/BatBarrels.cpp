#include "mb/game/BatBarrels.h"

#include "mb/engine/Log.h"

#include <algorithm>

namespace mb {

namespace {

// bat.x's "Common" animation keys time in ticks, 4800 to the second: the D3DX default, which the file does not override, and the
// spacing of its keys (160 ticks) is one frame of a 30 frame a second take [D].
constexpr double kTicksPerSecond = 4800;
constexpr const char* kTake = "Common";

constexpr double ticks(double count) {
    return count / kTicksPerSecond;
}

// Where the weapons' keys are, found by reading them out of the file (docs/GAMEPLAY.md 18.11) [D]. Every barrel is full size and
// has pushed its gun out by tick 4000 and stays so until its recoil; every recoil has settled by tick 11200; every barrel is gone by
// tick 16800. Where the recoil starts and where the shrinking starts differ by weapon: each barrel's keys stand still until then.
constexpr double kGrown = ticks(4000);
constexpr double kRecoilTo = ticks(11200);
constexpr double kShrinkTo = ticks(16800);

// The first key at which any frame of the barrel moves after it has stood (its recoil), and the last at which none of its frames has
// moved yet before it shrinks.
constexpr BarrelClip kClips[kWeaponTypeCount] = {
    // The Gun's bodies tilt in from tick 12800 (to 45 degrees by 14400) while their guns still stand: the first move of any frame
    {kGrown, ticks(7040), kRecoilTo, ticks(12800), kShrinkTo},  // Gun
    {kGrown, ticks(6880), kRecoilTo, ticks(12000), kShrinkTo},  // Cannon
    // The bazooka's barrel starts to ease back at 5440 (its next key is at 7360, with the others' recoil in full swing)
    {kGrown, ticks(5440), kRecoilTo, ticks(12000), kShrinkTo},  // Bazooka
    {kGrown, ticks(6880), kRecoilTo, ticks(12000), kShrinkTo},  // Laser
};

// The frames of bat.x the weapons use, one actor each, and which of them each weapon has.
constexpr const char* kFrames[] = {"Pulemet-Left", "Pulemet-Right", "Pushka", "Bazuka-Korpus", "lazer-Korpus"};
constexpr std::size_t kWeaponCount = kWeaponTypeCount;
constexpr std::size_t kFirstFrame[kWeaponCount + 1] = {0, 2, 3, 4, 5};  // by WeaponType: its frames are [first, next first)

Level framesLevel(std::string_view batMaterial) {
    Level level;
    for (const char* frame : kFrames) {
        GameObject object;
        object.type = ObjectType::Bat;
        object.mesh = "bat.x";
        object.frame = frame;
        object.material = std::string(batMaterial);
        Actor actor;
        actor.object = object;
        level.actors.push_back(actor);
    }
    return level;
}

}  // namespace

BarrelClip barrelClip(WeaponType type) {
    return kClips[static_cast<std::size_t>(type)];
}

void BarrelClock::update(bool wanted, double seconds) {
    switch (phase_) {
        case BarrelPhase::Hidden:
            if (wanted) {
                phase_ = BarrelPhase::Growing;
                time_ = 0;
            }
            break;
        case BarrelPhase::Growing:
            if (!wanted) {
                phase_ = BarrelPhase::Receding;
                break;
            }
            time_ += seconds;
            if (time_ >= clip_.grown) {
                phase_ = BarrelPhase::Ready;
                time_ = clip_.grown;
            }
            break;
        case BarrelPhase::Ready:
            if (!wanted) {
                phase_ = BarrelPhase::Shrinking;
                time_ = clip_.shrinkFrom;
            }
            break;
        case BarrelPhase::Recoiling:
            time_ += seconds;
            if (time_ >= clip_.recoilTo) {
                phase_ = BarrelPhase::Ready;
                time_ = clip_.grown;
            }
            break;
        case BarrelPhase::Receding:
            if (wanted) {
                phase_ = BarrelPhase::Growing;
                break;
            }
            time_ -= seconds;
            if (time_ <= 0) {
                phase_ = BarrelPhase::Hidden;
                time_ = 0;
            }
            break;
        case BarrelPhase::Shrinking:
            if (wanted) {
                phase_ = BarrelPhase::Growing;
                time_ = 0;
                break;
            }
            time_ += seconds;
            if (time_ >= clip_.shrinkTo) {
                phase_ = BarrelPhase::Hidden;
            }
            break;
    }
}

void BarrelClock::fired() {
    if (phase_ == BarrelPhase::Ready || phase_ == BarrelPhase::Recoiling) {
        phase_ = BarrelPhase::Recoiling;
        time_ = clip_.recoilFrom;
    }
}

BatBarrels::BatBarrels(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures, std::string_view batMaterial)
    : frames_(cache, materials, textures, framesLevel(batMaterial), PhysicsPlan{}) {
    const ModelCache::Entry* entry = cache.model("bat.glb");
    if (entry == nullptr) {
        return;
    }
    model_ = &entry->model;
    animation_ = model_->findAnimation(kTake);
    if (animation_ == nullptr) {
        logWarn("bat.glb has no '{}' animation, so the weapons' barrels are not drawn: convert the meshes again (tools/convert_assets.py)",
                kTake);
    }
}

void BarrelClocks::update(std::optional<WeaponType> armed, std::span<const GameEvent> events, double seconds) {
    for (const GameEvent& event : events) {
        if (event.type == EventType::ShotFired && event.prize) {
            if (const std::optional<WeaponType> type = weaponOf(*event.prize)) {
                clocks_[static_cast<std::size_t>(*type)].fired();
            }
        }
    }
    for (std::size_t type = 0; type < clocks_.size(); ++type) {
        clocks_[type].update(armed && static_cast<std::size_t>(*armed) == type, seconds);
    }
}

void BatBarrels::update(const GameSession& session, std::span<const GameEvent> events, double seconds) {
    const std::optional<WeaponState> armed = session.weapon();
    clocks_.update(armed ? std::optional<WeaponType>(armed->type) : std::nullopt, events, seconds);
}

void BatBarrels::append(const Mat4& place, std::vector<DrawItem>& out) const {
    if (animation_ == nullptr) {
        return;
    }
    for (std::size_t type = 0; type < kWeaponCount; ++type) {
        const BarrelClock& clock = clocks_[static_cast<WeaponType>(type)];
        if (clock.phase() == BarrelPhase::Hidden) {
            continue;
        }
        const std::vector<Mat4> poses = model_->globalPoses(*animation_, static_cast<float>(clock.time()));
        const std::size_t first = out.size();
        for (std::size_t frame = kFirstFrame[type]; frame < kFirstFrame[type + 1]; ++frame) {
            frames_.appendActorPosed(static_cast<ActorIndex>(frame), poses, out);
        }
        for (std::size_t i = first; i < out.size(); ++i) {
            out[i].world = out[i].world * place;
        }
    }
}

}  // namespace mb

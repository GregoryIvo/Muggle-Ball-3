#include "mb/game/Level.h"

namespace mb {

namespace {

std::size_t countIn(const std::vector<Actor>& actors, ObjectType type) {
    std::size_t n = 0;
    for (const Actor& a : actors) {
        if (a.object) {
            n += a.object->type == type;
            n += countIn(a.object->clothSegments, type);
        }
    }
    return n;
}

}  // namespace

std::vector<ActorRef> flattenActors(const Level& level) {
    std::vector<ActorRef> actors;
    for (const Actor& actor : level.actors) {
        const GameObject* object = actor.object ? &*actor.object : nullptr;
        actors.push_back({static_cast<ActorIndex>(actors.size()), &actor, object});
        if (object) {
            for (const Actor& segment : object->clothSegments) {
                actors.push_back({static_cast<ActorIndex>(actors.size()), &segment, object});
            }
        }
    }
    return actors;
}

std::size_t Level::countObjects(ObjectType type) const {
    return countIn(actors, type);
}

}  // namespace mb

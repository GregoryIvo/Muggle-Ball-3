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

std::unordered_map<std::uint32_t, ActorIndex> actorsById(std::span<const ActorRef> actors) {
    std::unordered_map<std::uint32_t, ActorIndex> byId;
    // try_emplace keeps the first one: an object id wins over a segment id, an earlier object over a later one.
    for (const ActorRef& ref : actors) {
        if (ref.actor->object) {
            byId.try_emplace(ref.actor->object->id, ref.index);
        }
    }
    for (const ActorRef& ref : actors) {
        if (ref.actor->object) {
            const std::size_t segments = ref.actor->object->clothSegments.size();
            for (std::size_t k = 0; k < segments; ++k) {
                byId.try_emplace(ref.actor->object->id + 1 + static_cast<std::uint32_t>(k),
                                 ref.index + 1 + static_cast<ActorIndex>(k));
            }
        }
    }
    return byId;
}

std::size_t Level::countObjects(ObjectType type) const {
    return countIn(actors, type);
}

}  // namespace mb

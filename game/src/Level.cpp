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

std::size_t Level::countObjects(ObjectType type) const {
    return countIn(actors, type);
}

}  // namespace mb

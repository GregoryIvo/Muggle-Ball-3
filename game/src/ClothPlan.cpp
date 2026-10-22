// The cloths of a level as the physics has them, see planCloths() in ClothPlan.h.

#include "mb/game/ClothPlan.h"

#include "mb/game/PhysicsScene.h"

#include <algorithm>
#include <cstdint>

namespace mb {

namespace {

// Clothmat's density (every cloth material has it) [D]: a segment's sphere of 0.5 weighs 0.0052 with it.
constexpr float kClothDensity = 0.01f;

}  // namespace

void planCloths(std::span<const ActorRef> actors, const MaterialLibrary& materials, PhysicsPlan& plan) {
    constexpr int kNone = -1;
    struct Place {
        int cloth = kNone;
        int particle = 0;
    };
    std::vector<Place> placeOf(plan.actors.size());

    for (const ActorRef& ref : actors) {
        if (!ref.actor->object || ref.actor->object->type != ObjectType::Cloth) {
            continue;
        }
        const GameObject& object = *ref.actor->object;
        const int width = object.cloth.width, height = object.cloth.height;
        const std::size_t count = width > 1 && height > 1 ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height) : 0;
        if (count == 0 || 1 + object.clothSegments.size() != count) {
            ++plan.unsupportedCloths;
            continue;
        }

        ClothPlan cloth;
        cloth.lead = ref.index;
        cloth.width = width;
        cloth.height = height;
        cloth.wind = object.cloth.wind;
        bool usable = true;
        for (std::size_t k = 0; k < count && usable; ++k) {
            const Actor& actor = k == 0 ? *ref.actor : object.clothSegments[k - 1];
            const auto sphere = std::ranges::find(actor.shapes, ShapeType::Sphere, &Shape::type);
            usable = sphere != actor.shapes.end() && sphere->radius > 0;
            if (usable) {
                cloth.actors.push_back(ref.index + static_cast<ActorIndex>(k));
                cloth.rest.push_back(transformPoint(rigidTransform(sphere->localPose).translationPart(), rigidTransform(actor.pose)));
                cloth.particleRadius = std::max(cloth.particleRadius, sphere->radius);
                if (actor.body) {
                    cloth.particleMass = std::max(cloth.particleMass, actor.body->mass);
                }
            }
        }
        if (!usable) {
            ++plan.unsupportedCloths;
            continue;
        }
        const GameMaterial* material = object.material.empty() ? nullptr : materials.find(object.material);
        if (material != nullptr) {
            cloth.friction = material->dynamicFriction;
            cloth.restitution = material->restitution;
        }
        if (!(cloth.particleMass > 0)) {  // the level files all give one; a made-up one may leave it to the material's density
            const float density = material != nullptr && material->density > 0 ? material->density : kClothDensity;
            cloth.particleMass = 4.0f / 3.0f * kPi * cloth.particleRadius * cloth.particleRadius * cloth.particleRadius * density;
        }

        const int index = static_cast<int>(plan.cloths.size());
        for (std::size_t k = 0; k < count; ++k) {
            placeOf[cloth.actors[k]] = {index, static_cast<int>(k)};
        }
        // One sphere a particle on the lead's body, in its frame: the sail's extent for whoever measures the actor.
        BodyDesc& body = plan.actors[ref.index].body;
        const Mat4 toLead = inverse(plan.actors[ref.index].pose);
        PhysicsShape sphere;
        sphere.type = PhysicsShapeType::Sphere;
        sphere.radius = cloth.particleRadius;
        if (!body.shapes.empty()) {
            sphere.material = body.shapes.front().material;
        }
        body.shapes.clear();
        for (const Vec3 p : cloth.rest) {
            sphere.localPose = Mat4::translation(transformPoint(p, toLead));
            body.shapes.push_back(sphere);
        }
        plan.cloths.push_back(std::move(cloth));
    }
    if (plan.cloths.empty()) {
        return;
    }

    // The joints that hold a sail: whichever end is a particle, the other is what it hangs from.
    for (const JointPlan& joint : plan.joints) {
        if (!joint.toCloth) {
            continue;
        }
        const Place a = joint.actorA == kNoActor ? Place{} : placeOf[joint.actorA];
        const Place b = joint.actorB == kNoActor ? Place{} : placeOf[joint.actorB];
        if ((a.cloth == kNone) == (b.cloth == kNone)) {  // a sail to a sail, or to the placeholder of one that is not planned: nothing holds
            continue;
        }
        const Place& mine = a.cloth != kNone ? a : b;
        const ActorIndex holder = a.cloth != kNone ? joint.actorB : joint.actorA;
        ClothPlan& cloth = plan.cloths[static_cast<std::size_t>(mine.cloth)];
        ClothPin pin;
        pin.particle = mine.particle;
        pin.holder = holder;
        const Vec3 rest = cloth.rest[static_cast<std::size_t>(mine.particle)];
        pin.local = holder == kNoActor ? rest : transformPoint(rest, inverse(plan.actors[holder].pose));
        pin.breakForce = joint.desc.breakForce;
        cloth.pins.push_back(pin);
    }
}

}  // namespace mb

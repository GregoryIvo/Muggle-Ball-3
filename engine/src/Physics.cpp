// STUB: replaced by the Jolt implementation.
#include "mb/engine/Physics.h"

namespace mb {

struct PhysicsWorld::Impl {};

PhysicsWorld::PhysicsWorld(Vec3, const PhysicsSettings&) : impl_(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() = default;
BodyId PhysicsWorld::addBody(const BodyDesc&) { return 0; }
void PhysicsWorld::removeBody(BodyId) {}
JointId PhysicsWorld::addJoint(const JointDesc&) { return 0; }
void PhysicsWorld::removeJoint(JointId) {}
int PhysicsWorld::step(float) { return 0; }
Mat4 PhysicsWorld::pose(BodyId) const { return {}; }
Vec3 PhysicsWorld::linearVelocity(BodyId) const { return {}; }
Vec3 PhysicsWorld::angularVelocity(BodyId) const { return {}; }
void PhysicsWorld::setLinearVelocity(BodyId, Vec3) {}
void PhysicsWorld::setAngularVelocity(BodyId, Vec3) {}
void PhysicsWorld::addImpulse(BodyId, Vec3, Vec3) {}
void PhysicsWorld::setPose(BodyId, const Mat4&) {}
void PhysicsWorld::setKinematicTarget(BodyId, const Mat4&) {}
MotionType PhysicsWorld::motionType(BodyId) const { return MotionType::Static; }
void PhysicsWorld::setMotionType(BodyId, MotionType) {}
bool PhysicsWorld::isAwake(BodyId) const { return false; }
void PhysicsWorld::wake(BodyId) {}
std::uint64_t PhysicsWorld::userData(BodyId) const { return 0; }
std::vector<ContactEvent> PhysicsWorld::takeContacts() { return {}; }
std::vector<JointId> PhysicsWorld::takeBrokenJoints() { return {}; }
std::optional<RayHit> PhysicsWorld::raycast(Vec3, Vec3, float) const { return std::nullopt; }
std::size_t PhysicsWorld::bodyCount() const { return 0; }
std::size_t PhysicsWorld::awakeBodyCount() const { return 0; }
std::size_t PhysicsWorld::jointCount() const { return 0; }

}  // namespace mb

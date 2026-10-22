#pragma once

#include "mb/engine/Math.h"
#include "mb/game/BallReach.h"
#include "mb/game/GameCamera.h"
#include "mb/game/Level.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/PhysicsScene.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mb {

// The rules of one level being played, as docs/GAMEPLAY.md measured them from footage of the original: the bat, the
// balls, bricks that take hits and explode, the score, the hearts, the end of the level. GameSession drives a
// PhysicsScene and reports what happens as events; it draws nothing and plays nothing. The presentation reads the state
// every frame, takes the events, and gives the session the cursor and the click:
//
//     std::optional<GameSession> session;  // not copyable or movable: it holds the physics world
//     session.emplace(level, materials, SessionOptions{.hearts = 2});
//     // every frame:
//     session->update(dt, {.batTargetX = batTargetX(mouseX, mouseY, w, h, camera), .launch = clicked});
//     for (const GameEvent& event : session->takeEvents()) { /* sounds, flashes, the HUD */ }
//     // drawing: session->scene().actorPose(i) for each actor with !session->actor(i).removed, session->bat(),
//     // session->balls(); retexture a brick by session->actor(i).textureIndex and draw it at 1 - actor(i).shrink of its
//     // size; the HUD from hearts() and score().
//
// What the presentation reads is the state after the last fixed step (1/60 s, see PhysicsSettings::fixedStep); update()
// does not interpolate, and a display running at another rate may carry a ball on with its velocity.
//
// Space is the level's (Math.h): +y up, +z away from the player. The ball moves on the horizontal plane y = 0.6 and
// the bat at the near edge, z about -21, along x only.

// Every number the rules use. The comment says where it comes from: the section of docs/GAMEPLAY.md and how well it
// is known: [M] measured from footage, [D] read from the game's data, [I] inferred, [?] a guess.
struct GameplayRules {
    // ---- Ball (section 2)
    float ballSpeed = 15.0f;  // units/s, planar and constant: set again after every fixed step [M]
    float ballPlaneY = 0.6f;  // where its centre stays; its radius, so it rides on the floor [M]
    // The physics bounces the ball off walls and bricks, friction and spin included (the walls turn it by 11-18 degrees
    // on most bounces, both ways; "Corrections" [M]), and only its speed is set back. Friction takes the sideways speed
    // off a ball that bounces between two parallel walls, though, until it goes straight up and down for ever (or along
    // x, off the side walls). So after a bounce off anything but the bat, a path closer than this to the x axis or to
    // the z axis is turned out to that angle, away from the axis and to the side it was going. The footage has no path
    // this flat (the bat's exits reach 31 degrees from +z) and none this straight but a centre hit of the bat, which is
    // never touched, so it does not contradict it [?].
    float minPlanarAngleDegrees = 10.0f;   // from the x axis
    float minVerticalAngleDegrees = 5.0f;  // from the z axis (3.4 degrees is the least seen)
    // Outside this box, a ball went through a wall it should not have: it is taken out and counts as lost [?].
    float escapeX = 40.0f, escapeZ = 40.0f;

    // ---- Launch and loss (section 4 and "Corrections")
    float launchAngleDegrees = 11.0f;  // left of +z, whatever the offset on the bat: 10.7 to 11.2 [M] (first read 11.5)
    float heldOffsetX = -0.75f;        // the held ball, from the bat's centre: 0.25 to 0.8 left [M]
    float heldOffsetZ = 3.7f;          // in front of it, z about -17.4 [M]
    // A ball whose centre passes the bat's line is lost, and shrinks away over ballVanishTime (V1 7874.10 to 7874.35;
    // at z = -23.5 it is gone) [M]. The session takes it out at once; the presentation carries it on for the shrink
    // from the BallLost event (position and velocity).
    float ballLostZ = -21.0f;
    float ballVanishTime = 0.25f;

    // ---- Bat (section 3)
    float batRadius = 3.0f;       // collision sphere [D]
    float batZ = -21.0f;          // centre; about -20.4 if the ball's centre is at y = 0 [M]
    float batLimitX = 16.0f;      // the centre stays within +-this: seen from -15.9 to +16.1 [M, ?]
    // The bat follows the cursor like a critically damped spring (rate in 1/s): a 4.5 unit move peaks at
    // 4.5 * rate / e = 42 units/s, the ramp of V1 60.9-61.1, and a 30 unit one at 280, above the 60 the 1/15 s
    // samples need. There is no speed cap in the footage [M].
    float batResponse = 25.0f;
    // Whatever the incoming direction, the ball leaves along (ball - bat) in the plane (section 3 [M]). One that meets
    // the bat's flank would leave nearly flat or back: the exit is held to this angle from +z, so it always leaves with
    // a forward part. Footage reaches 31 degrees (edge hits unseen) [?].
    float batMaxExitDegrees = 70.0f;

    // ---- Lives. Seconds after the ball passed the bat's line, V1 7874.10-7877.07 [M] ("Corrections")
    int startHearts = 2;           // spare lives; a heart is spent as the new bat appears [M]
    double batLeaveDelay = 0.6;    // the bat starts to fly off (7874.7)
    double batLeaveTime = 0.4;     // and is gone (7875.1)
    double heartDelay = 2.1;       // a heart is spent (7876.17-.20); with none left, the game is over
    double batEnterDelay = 2.1;    // the new bat starts to swoop in with it (7876.23), never before the heart
    double readyDelay = 2.63;      // the ball is on the bat and can be launched (7876.73)
    double startReadyDelay = 0.7;  // at the start of a level the swoop takes this long (V1 52.2-52.9)

    // ---- A ball that cannot move [?]
    // A ball can be pinned: between the bat at its limit and the side wall, or in a pocket between a wall and a brick
    // that is narrower than itself, where friction and the bounces that turn it out of a flat path (above) keep it
    // still. A player moves the bat, which frees the first kind; nothing frees the second, and the footage shows
    // neither. A ball that has stayed within jamDistance of where it was for jamTime seconds is put back in the world
    // (a body with none of the contact state that holds the old one) on a path towards the middle of the board.
    float jamDistance = 1.0f;
    double jamTime = 1.0;

    // ---- Targets the ball cannot get at (section 5.10; the end of the level, see GameSession)
    // The grid the ground of the ball's plane is cut into for the look at what it can reach, BallReach.
    float reachCell = 0.2f;
    // What the look cannot tell: a piece that stays up where the physics left it (a mast held by joints that no longer
    // hold it, a cactus that came to rest 10 cm above the ball's reach) is a target nobody can hit. When balls have been in
    // flight this long without touching any brick and no more than stallMost targets, or stallShare of those the level
    // began with, are left, they are let go and the level ends. The autopilot's runs that stood still (14 of the 60 levels
    // it finished in 900 s, 8 of which it would not have finished without this) did so with 1 to 6 targets left [?].
    double stallSeconds = 90;
    int stallMost = 12;
    float stallShare = 0.25f;

    // ---- Bricks (section 5)
    // A hit is a contact that begins with the ball closing on the thing at more than this share of its speed along
    // the contact normal (4.5 units/s of 15), and a ball laid on the floor or skimming past touches at none. A
    // ball that slides along a row of tiles meets the edge of each next one at up to a quarter of its speed, which
    // must not hurt it [I]. (Joints break at 2 units/s, the bounce threshold; that is a different thing.)
    float hitFraction = 0.3f;
    // A dynamic body below this has left the board: it stops being a target and is taken out (ActorFell), with no
    // explosion. It is not destroyed. Loose pieces are destroyed by weapons and blasts (the Laser beam, lightning bolts,
    // dynamite chains: step 5), not by falling or sinking: nothing that fell, sank or drifted died in the footage
    // ("Rules settled by the second study" [M]). So it scores nothing; fallScores adds its brickScore, to try the other
    // reading.
    float fallLimitY = -5.0f;
    bool fallScores = false;
    // A person or an animal (a material that shrinksAway()) pays its score as it is destroyed and is gone this long after,
    // shrinking away meanwhile; a brick of another kind is gone with its score. The level ends when the last required
    // object is gone, so after the shrink. The footage has them gone 0.35 to 0.4 s after the score (level 1's last pirate:
    // paid 143.75, gone 144.15; level 17's gorilla: paid 1943.6, gone about 1944.0; docs/GAMEPLAY.md, "Rules settled by the
    // second study", 8) [M].
    double shrinkTime = 0.35;
};

// Which objects must be destroyed to finish a level. The second study of the footage and its fact-check settled it
// (docs/GAMEPLAY.md, "Rules settled by the second study", 3 and 7): Bricks. BrickType, the other reading docs/OBJECTS.md
// section 10 weighed, is refuted; it stays so that the autopilot's runs and the tests can still make a target out of a
// brick of one kind. Never is for a session that never ends.
enum class TargetRule {
    // Every damageable brick (people, buildings, ships, ground, rocks, crates, animals) and every tree, whatever its
    // motion: dynamic, kinematic (floor tiles, sharks, birds) or bound to a path. Not required: every object of a type
    // other than brick and tree: metall, teleports and cannons (left on the board at natural ends), and, never seen to
    // decide a level's end, dynamite, fireworks, generators, springs, volcanoes, cloth and vibro. Those can still be
    // destroyed for their points.
    Bricks,
    // Every brick whose material has brickType 0, moving or not; crates, rocks, animals, ground (type 4) and trees (5) do
    // not count. The footage refutes it: a level did not end before its type 4 gorilla was destroyed.
    BrickType,
    // Nothing is a target: the level does not end (sandbox, measuring).
    Never,
};

struct SessionOptions {
    GameplayRules rules;
    // Bricks is the default: the rule the footage shows (a level ends when the last brick or tree is gone, moving or not,
    // with metal, portals and cannons still on the board). The autopilot's numbers for it are in docs/GAMEPLAY.md section
    // 12 (tests/gameplay_smoke.cpp counts them), but it cannot finish a level that has a target out of the ball's reach
    // (above it on a support that stays, or beyond the walls: 43 of the 83 have, most of them knight boards), so a target
    // the ball cannot get at is not one: see ignoreUnreachable.
    TargetRule levelEnd = TargetRule::Bricks;
    // A target does not count once the ball cannot get at it, so that no level ends up impossible to finish. The ball
    // gets at what it can touch on its plane (a part of the plane joined to the bat's end of the board without a gap
    // narrower than itself, bounded by the immovable parts of the level: walls, platforms, pinned buildings, and what is
    // beyond the board's edge), and at what stands above the plane over ground it can be on, which falls if what holds
    // it is a brick. So a target is out of play when
    //  - it is within the ball's height and no ground it can be on is within a ball's radius of it: it is behind a wall,
    //    beyond the edge of the board or in a pocket too narrow for the ball;
    //  - it is higher, and is held where it is (pinned to the level, on a path, or a body that has no gravity) or has no
    //    ground it can be on under it (it rests on a platform, a wall top or a building that the ball cannot touch or
    //    destroy).
    // The look is made when the level starts and then twice a second at every target that is a loose body (bricks fall
    // and are thrown): what is out of play stays so, and counts for nothing in targetsLeft(), isTarget() and the end of
    // the level. Weapons would reach more (step 5). False keeps every target of the rule, but for the last resort of
    // GameplayRules::stallSeconds, which does not depend on it.
    bool ignoreUnreachable = true;
    std::optional<int> hearts;  // carried over from the level before; GameplayRules::startHearts for the first
    std::int64_t score = 0;     // likewise
    PhysicsOptions physics;
    PhysicsSettings physicsSettings;
};

// ---- What the session reports

enum class EventType {
    BallLaunched,    // ball, position (a held ball was let go, or addBall())
    BatHit,          // ball, position (of the ball), speed (the bat's own, along x)
    BoardHit,        // ball, position (the contact), speed: the ball met a wall (the sound "ballHitBoard")
    BallsHit,        // ball, otherBall, position, speed ("ballHitBall")
    BrickHit,        // ball (-1 for hit()), actor, position (the contact), speed, energy (left), material
    // BrickDestroyed comes exploseDelay milliseconds after the hit that took the last energy. A person or an animal
    // (shrinksAway()) is then still there for GameplayRules::shrinkTime, shrinking (ActorStatus::shrink): the others are gone.
    BrickDestroyed,  // actor, position (centre of mass), points (added to the score), material
    ActorFell,       // actor, position (centre of mass), points (only with fallScores), material: it left the board
    BallLost,        // ball, position, velocity: behind the bat's line (or out of the world); it shrinks away
    BallFreed,       // ball, position: it was pinned and is put back in the world (see GameplayRules::jamTime)
    LifeLost,        // hearts (left): the heart is spent, 2.1 s after the last ball was lost
    BatLeft,         // the bat starts to fly off (after the last ball was lost)
    BatEntered,      // the bat swoops in with the ball on it (at the start of a level, and as the heart is spent)
    LevelComplete,   // the last target is gone (a shrinking one is gone when it has shrunk)
    GameOver,        // the last ball is lost with no heart left
};

struct GameEvent {
    EventType type = EventType::BallLaunched;
    double time = 0;  // seconds of play since the session began
    int ball = -1;    // BallView::id
    int otherBall = -1;
    ActorIndex actor = kNoActor;
    Vec3 position{};
    Vec3 velocity{};
    float speed = 0;
    int energy = 0;
    int points = 0;
    int hearts = 0;
    // The brick's game material, for the sounds (hitSound, deadSound), the explosion (exploseType) and the retexture;
    // it points into the MaterialLibrary the session was made with. Null where there is none.
    const GameMaterial* material = nullptr;
};

// One line of text for an event (for logs and for reading a recorded game).
std::string describe(const GameEvent& event);

// ---- State the presentation reads each frame

// Where the session stands. The bat's own phases are in BatView.
enum class SessionPhase {
    Entering,  // a bat swoops in with a ball held on it; it cannot be launched yet
    Ready,     // the ball waits on the bat for the click
    Playing,   // balls in flight
    Lost,      // the last ball was lost; the bat leaves, a heart is spent, a new bat comes (BatView says where)
    Complete,  // the level is done: nothing more is lost or launched, bricks still explode
    GameOver,  // the last ball was lost with no heart left
};

enum class BatPhase {
    Entering,  // swooping in
    Present,
    Leaving,   // flying off
    Absent,
};

struct BatView {
    BatPhase phase = BatPhase::Entering;
    double phaseTime = 0;  // seconds since the phase began, for animating the swoop
    // How long Entering lasts (startReadyDelay at the start of a level, readyDelay - batEnterDelay after a loss) and
    // Leaving (batLeaveTime); 0 for the others.
    double phaseDuration = 0;
    Vec3 position{};  // the centre of its collision sphere; where it will be or was while it swoops or leaves
    float velocityX = 0;
};

struct BallView {
    int id = 0;  // unique in the session
    Vec3 position{};
    Vec3 velocity{};  // zero while held; the rules' own (always ballSpeed along the path), not what the solver left
    bool held = false;  // on the bat, waiting for the click
};

// One actor of the level as the rules see it.
struct ActorStatus {
    bool damageable = false;  // a ball hurts it (canBeDamaged()) and it is still there
    bool exploding = false;   // out of energy, waiting out exploseDelay
    // After its score, a person or an animal shrinks away over GameplayRules::shrinkTime: 0 as it begins, up to 1 as it is
    // removed. It is a target until then, and no more to be hurt.
    float shrink = 0;
    bool removed = false;     // it exploded or fell off the board, or finished shrinking: stop drawing it
    int energy = 0;           // hits left
    int maxEnergy = 0;
    int textureIndex = 0;     // which of the material's textures shows now, see damageTextureIndex()
    bool target = false;      // it must be destroyed to finish the level, by the session's TargetRule
    bool outOfPlay = false;   // the ball cannot get at it, so it does not count (SessionOptions::ignoreUnreachable)
};

struct SessionStats {
    int ballsLaunched = 0, ballsLost = 0, ballsFreed = 0, batHits = 0;
    int bricksHit = 0, bricksDestroyed = 0, actorsFallen = 0;
    int targetsOutOfPlay = 0;  // targets of either rule the ball could not get at, found so far
    int targetsLetGo = 0;      // targets that were let go because the level stood still (GameplayRules::stallSeconds)
};

// What the player does in a frame.
struct GameInput {
    // Where the cursor is on the board (batTargetX(), which has none for a cursor above the horizon); the bat eases
    // towards it. None keeps the target of the frame before.
    std::optional<float> batTargetX;
    bool launch = false;  // the click: lets the held ball go
};

// ---- Rules that stand on their own

// Whether balls hurt an actor of this object type with this material: it has a brick material (brickType other than
// 2, which the ball, bullets, board, water and the fallback have, and energy above 0). The board and markers never
// do. A cloth is a placeholder for a sail the cloth simulation has yet to build (step 5), so a ball goes by it, though
// the original scored sails (60 each: piratos-ostrovok's 62 bricks and 3 sails are the 65 bricks, 4,231 points of
// docs/GAMEPLAY.md), and they are not targets here.
bool canBeDamaged(ObjectType type, const GameMaterial* material);

// Whether a destroyed object of this material shrinks away instead of being gone with its score: exploseType 5, the
// class of the materials with no splinters to break into (people, animals, trees, fruit, barrels, the interactive objects),
// which docs/OBJECTS.md section 9 lists. The footage shows it for a pirate and a gorilla; the rest of the class is
// inferred.
bool shrinksAway(const GameMaterial* material);

// Whether an object counts toward the end of the level under a rule.
bool isTarget(TargetRule rule, ObjectType type, const GameMaterial* material);

// Which of a material's textures shows at this energy. Several textures on a material that does not animate are damage
// states listed from the most damaged to the intact one, so a brick shows the one at index energy - 1 (SceneBuilder
// does the same for a fresh brick). Zero for anything else.
int damageTextureIndex(const GameMaterial& material, int energy);

// The point of the plane y = planeY under a cursor at (cursorX, cursorY) in a window of width x height points (the
// origin at the top left, y down), seen with this view and projection; none when the ray misses the plane (the
// cursor above the horizon).
std::optional<Vec3> cursorOnPlane(float cursorX, float cursorY, float width, float height, const Mat4& view,
                                  const Mat4& projection, float planeY);
// The bat's target for a cursor: x of the point of the ball's plane under it, seen through `camera` at the window's
// aspect. None where there is no such point; keep the last target then.
std::optional<float> batTargetX(float cursorX, float cursorY, float width, float height, const Camera& camera,
                                float planeY = GameplayRules{}.ballPlaneY);

// ---- The session

class GameSession {
public:
    // Builds the level's physics (bodies asleep, as PhysicsScene does) and starts the first bat swooping in. The
    // library must outlive the session: events and ActorStatus point into it.
    GameSession(const Level& level, const MaterialLibrary& materials, const SessionOptions& options = {});

    // Plays `seconds` of the game in fixed steps of the physics' size (the rest carries over; at most
    // PhysicsSettings::maxStepsPerCall steps per call, so a long pause does not catch up). The target counts from now
    // on; a click waits for the next step, which launches the held ball if there is one to launch and drops it if not.
    //
    // Each step: the bat eases towards the target and the held ball follows it; the physics runs; then
    //  - a ball that touched the bat leaves along (ball - bat) in the plane (BatHit);
    //  - every ball's speed goes back to GameplayRules::ballSpeed (it stays on its plane by itself), and after a
    //    bounce a path too near an axis is turned out; one that cannot move is freed (BallFreed); one behind
    //    ballLostZ is lost (BallLost), and the last of them starts the life sequence: the bat leaves (BatLeft), a
    //    heart is spent (LifeLost) as the new bat comes (BatEntered) with the ball on it, and it can be launched
    //    again (phase Ready). With no heart left it is GameOver instead;
    //  - a ball that touches a brick at more than hitFraction of its speed hits it (BrickHit): one hit takes one
    //    energy, at zero the brick explodes exploseDelay milliseconds later (BrickDestroyed): its brickScore is added and
    //    its body goes (and what rested on it falls), a person's or an animal's after shrinking away for
    //    GameplayRules::shrinkTime;
    //  - dynamic bodies below fallLimitY are taken out (ActorFell), no longer targets, with no points unless fallScores;
    //  - when no target is left, LevelComplete.
    void update(double seconds, const GameInput& input);

    // The events since the last call, in the order they happened.
    std::vector<GameEvent> takeEvents();

    SessionPhase phase() const { return phase_; }
    double time() const { return time_; }
    std::int64_t score() const { return score_; }
    int hearts() const { return hearts_; }
    const BatView& bat() const { return bat_; }
    std::span<const BallView> balls() const { return views_; }
    const SessionStats& stats() const { return stats_; }
    const GameplayRules& rules() const { return rules_; }
    TargetRule levelEnd() const { return levelEnd_; }

    ActorStatus actor(ActorIndex actor) const;
    // Whether an actor that is still in the level counts toward a rule (whatever rule the session ends by), and how
    // many of them are left.
    bool isTarget(ActorIndex actor, TargetRule rule) const;
    int targetsLeft(TargetRule rule) const;

    // What the presentation draws the level's actors from (PhysicsScene::actorPose()).
    const PhysicsScene& scene() const { return scene_; }
    // Where an actor is now as a point for an effect or an aim: the centre of mass of its body (its pose is the model's
    // origin, which can be well away from the pieces).
    Vec3 centreOf(ActorIndex actor) const;

    // A ball in flight at `position` along `direction` (in the plane; the rules set its speed). For multiball and
    // for tests. Returns its id, or -1 if the world refused the body. It is no held ball, and losing it costs a life
    // only if it is the last.
    int addBall(Vec3 position, Vec3 direction);

    // One hit on an actor from something other than a ball (a bullet, dynamite, the Thunderbolt prize): the same as a
    // ball's, with `ball` -1 in the event. False if nothing counted: the actor is not damageable or is gone, shrinking
    // away or already out of energy.
    bool hit(ActorIndex actor, Vec3 at);

    // Ends the level at once (the prize "Next Level"). Nothing happens if it is over.
    void forceLevelComplete();

private:
    struct Ball {
        int id = 0;
        BodyId body = 0;  // none while held
        bool held = false;
        bool onBat = false;     // touched the bat at the last step: one hit for a stay on it
        bool bounced = false;   // began to touch something besides the bat in this step
        Vec3 heading{0, 0, 1};  // unit, in the plane: where it goes, kept for the step in which it stands still
        Vec3 anchor{};          // where it was when jamDistance last was exceeded, and when
        double anchoredAt = 0;
    };
    struct Brick {
        const GameMaterial* material = nullptr;
        ObjectType type = ObjectType::Unknown;
        bool damageable = false;
        bool exploding = false;
        bool shrinking = false;  // paid for, and on its way out (shrinksAway())
        bool removed = false;
        bool outOfPlay = false;  // a target the ball cannot get at: it does not count
        int energy = 0;
        int maxEnergy = 0;
        double explodeAt = 0;
        double removeAt = 0;  // when it has finished shrinking
        std::uint8_t targetFor = 0;  // bit per TargetRule
    };
    // What comes next after the last ball is lost.
    enum class Life { None, BatStays, BatLeaving, HeartDue, BatDue };

    GameplayRules rules_;
    TargetRule levelEnd_;
    PhysicsScene scene_;
    double fixedStep_;
    std::vector<Brick> bricks_;  // by ActorIndex
    std::vector<ActorIndex> dynamicActors_;
    std::optional<BallReach> reach_;  // null with ignoreUnreachable off
    std::vector<ActorIndex> loose_;   // the targets that are loose bodies and still count: looked at again and again
    int targetsLeft_[2] = {0, 0};     // by TargetRule
    int targetsAtStart_[2] = {0, 0};
    double stalled_ = 0;              // seconds of play since a brick was last touched
    std::vector<ActorIndex> exploding_;
    std::vector<ActorIndex> shrinking_;
    std::vector<ActorIndex> hitThisStep_;

    std::int64_t score_;
    int hearts_;
    double readyAt_;  // when the ball on the entering bat can be launched
    SessionPhase phase_ = SessionPhase::Entering;
    double time_ = 0;
    double accumulator_ = 0;
    std::uint64_t ticks_ = 0;
    bool launchPending_ = false;

    BatView bat_;
    BodyId batBody_ = 0;
    float batX_ = 0, batVelocity_ = 0, targetX_ = 0;
    bool snapBat_ = false;
    double batPhaseStart_ = 0;

    std::vector<Ball> balls_;
    std::vector<BallView> views_;
    int nextBall_ = 0;

    Life life_ = Life::None;
    double lostAt_ = 0;
    SessionStats stats_;
    std::vector<GameEvent> events_;
    bool eventsDropped_ = false;

    void tick();
    void moveBat();
    void enterBat();
    void setBatPhase(BatPhase phase, double duration = 0);
    int newBall(Vec3 position, Vec3 heading, bool held);  // -1: the world refused the body, and there is no ball
    void launchHeldBall();
    Ball* ballOf(BodyId body);
    void processContacts();
    void hitBat(Ball& ball);
    bool hitBrick(ActorIndex actor, Vec3 at, float speed, int ball);
    void keepBalls();
    void loseBall(std::size_t index, Vec3 position, Vec3 velocity);
    bool freeBall(Ball& ball, Vec3 at);
    void runLifeSequence();
    void explodeDue();
    void removeShrunk();
    void removeFallen();
    void removeBrick(ActorIndex actor);
    bool inPlay(ActorIndex actor) const;
    void releaseTargets(bool onlyLoose);
    void dropTarget(ActorIndex actor);
    void letGoOfStalled();
    void checkComplete();
    void complete();
    void emit(GameEvent event);
    void refreshViews();
};

}  // namespace mb

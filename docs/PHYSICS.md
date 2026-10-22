# Levels in the physics world

`PhysicsWorld` (`engine/`, Jolt underneath) is the PhysX 2.x model; `PhysicsScene` (`game/`) puts a level into it.
`planPhysics()` is the pure part: a `Level` and the `MaterialLibrary` in, body and joint descriptions out, with
no physics world and no GPU. `PhysicsScene` builds the world from that, steps it, and carries the test balls.
Every actor of a level has a stable index (`flattenActors()`: top-level actors in file order, each cloth followed
by its segments), which draws (`SceneDraw::actor`), bodies (`BodyDesc::userData`) and joints share.

## What a level becomes

| Level data | Physics |
|---|---|
| actor pose, shape and mass poses | rigid (`orthonormalised()`, the nearest rotation): 5 actors and 34 shape poses were saved slightly scaled. Drawing uses the same pose, so a body and its model agree |
| box, sphere, capsule (`halfHeight = height / 2`), convex (`hullPoints`) | the same shapes; planes, triangle meshes and hulls without points are counted and left out. A capsule whose cylinder is shorter than 10 cm (20 bags and rocks, half heights 0.005 to 0.037) is a sphere: Jolt's capsule sinks through a 0.84 thick floor when the cylinder is that short (radius 1.4, half height 0.01: 35 of 40 random drops), the sphere is within 4 cm of it |
| body flags | all six frozen bits (the board 0xFF, the markers 0x7F) -> static; kinematic -> kinematic; else dynamic, frozen bits as locks, "disable gravity" as no gravity. Dynamic bodies have swept (continuous) collision |
| mass, inertia, mass frame, damping, spin cap, sleep thresholds | as saved |
| friction, restitution, combine modes | the game material the **owning object** names (a cloth segment: its cloth's); none, or one the library lacks (3 bricks and 11 flickers), gives 0.5 / 0.5 / 0. `Shape::materialIndex` is a per-level handle and is not used |
| group 5 (markers of lights, shadow casters, backgrounds) | no shape, so no body (148 actors); they sit inside the board and collide with nothing |
| group 11 (one box per board at the player's end) | not in the simulation; kept as `PhysicsPlan::zones` for the bat and the lost-ball test (step 4). Trigger shapes (flags 0x7, the teleport sources) are zones too |
| actors that follow a path (35 sharks and birds, object flag 0x01) | kinematic bodies that `PhysicsScene::step` moves along their path, see "Path actors". Put on the path before the first step |
| cloth: 22 sails, their 431 segments and the 22 cloth objects' own actors (453 particles) | kinematic in the plan, with no body of their own: a sail is one cloth body (Jolt's soft body, "Cloth" below) whose particles start at the saved poses. The 84 joints that hold their corners (75 ball joints, 9 fixed) are pins of the cloth, not joints of the world |
| joint actors | resolved as `LEVEL_FORMAT.md` says (0 = world, else object id, else cloth segment); all 4,895 resolve. Fixed -> fixed, revolute -> hinge, spherical -> ball. `collideConnected` is false |
| fixed joints to the world that never break | the body is static and the joint is dropped, see "Welded to the world" |

## The starting state

The levels were saved from a running simulation. All 83 levels, 10 s, no ball; "moved" is dynamic and kinematic
bodies that ended more than 2 cm from where they began, "broke" joints (9,734 dynamic and 3,414 kinematic bodies,
4,256 joints once the welds to the world and the cloth's are taken out). Reproduce with `mb_physics_smoke --measure`.

| Start | Joint breaking | Moved | Broke | Furthest / fastest |
|---|---|---|---|---|
| **A** asleep, zero velocity, machines awake | by load | 211 | 165 | 406 / 89 |
| B awake, saved velocities | by load | 3,207 | 3,208 | 491 / 98 |
| C awake, zero velocity | by load | 3,200 | 3,214 | 491 / 98 |
| B, C with the joint frames as saved | by load | 3,312, 3,251 | 3,236, 3,220 | same |
| **A** | **by impact** | **71** | **0** | **19.6 / 10** |
| B | by impact | 1,069 | 2 | 491 / 98 |
| C | by impact | 1,009 | 2 | 491 / 98 |
| C with the joint frames as saved | by impact | 1,586 | 2 | 491 / 98 |
| A and a test ball | by load, by impact | 1,919, 1,637 | 1,171, 239 | 410, 354 / 89, 82 |

A moves 71 bodies in five levels: the 51 machine bodies of the windmills and the Ferris wheel and 20 that rest on
them (a windmill's tower settles by up to 0.25 under the sails). Everywhere else nothing moves and nothing breaks.

Only A leaves the other 78 levels as designed: awake, the overlaps the data saved (up to 1.4 deep) push about a
thousand bodies apart, the saved velocities fling some off the board (the furthest 491 units is a body falling for
the whole run), and by load a third of all joints break. (With Jolt's own 10 / 2 iterations, awake and judged by
impact, 2,338 bodies moved and 138 joints broke: much of that was the solver, see below.) So every moving body starts
asleep with its velocity dropped, with one exception: the machines.

**Joint frames.** 433 of the 4,340 joints that are built (10%) were saved stretched by more than 5 cm, 5 by more
than a unit, the worst 11.3 (`Piratos-Kartejniki`). With the stored frames a woken joint pulls its bodies together:
in the first 0.1 s awake, all levels together, 948 bodies move more than 2 cm and the furthest 1.54 units; with the
second body's frame rebuilt from the saved poses (`PhysicsOptions::rebuildJointFrames`) it is 512 and 0.68 (the rest
is the overlaps). Each joint then starts at rest and nothing snaps. (1,716 of the 4,100 fixed joints between two
bodies have their anchor more than 2 units from both: a weld has no pivot, and the solver copes with the lever.)

## The solver

Jolt's own 10 velocity and 2 position iterations are not enough for these levels. Awake, a windmill's tower (eight
stones of 1,800 and a roof of 5,110, kept awake by the sails that turn on it) sinks and topples within a minute and
the loads it sheds wake the neighbourhood; stacks of bricks sink and tilt, and joined models tear apart. Measured on
the four windmill levels for 240 s with no ball (the furthest any body that is no machine ended from where it
started):

| velocity / position / collision steps | furthest body | joints broken | ms per fixed step |
|---|---|---|---|
| 10 / 2 / 1 (Jolt) | 103,060 | 45 | |
| 40 / 1 / 1 | 66,053 | 26 | 0.07 |
| 60 / 1 / 1 | 24 | 27 | 0.07 |
| **100 / 1 / 1** | **0.22** | **0** | **0.12** |
| 30 / 1 / 2 | 0.26 | 0 | 0.14 |
| 10 / 1 / 4 | 0.17 | 0 | 0.21 |

So `PhysicsSettings` is 100 velocity iterations and 1 position iteration (0 is not usable: bodies never fall asleep
and some sink into the floor). A step costs 0.09 ms on average with a ball in the level and 1.3 ms in the slowest
of the 1,992 runs of `mb_physics_smoke --shots 24`. Worker threads are capped at 3: nine workers took twice as long as three on
the biggest level and used nine cores.

## Welded to the world

555 fixed joints tie 551 bodies to the world with a maxForce that never breaks (trees, fences, rocks, cacti, mirrors
of fences). In the original such a body never moves. Built as a body and a joint, the weld has the file's anchor,
which is the world origin for 553 of the 570 world welds, up to 22 units from the body: a lever that turned a fence
post into a pendulum. A ball that hit it sent it off at Jolt's speed limit (500 units per second), taking
its neighbours along (a review sweep of one ball per second for 8 s at every level found two such runs in 249:
piratos-sklep and village-dinner). So such a body is static and the joint is dropped. The one world weld that can break (village-drovoseki, at 100) stays
a joint.

## Machines

Nine revolute joints have a motor: the roof and the sails of the windmills in `village-dinner`, `village-na-melnice`,
`village-rinok` and `Village-Dvorik` (0.5 and 2 rad/s, torque 100) and the wheel of `village-park` (3 rad/s, torque
80, on a kinematic frame). The levels were saved with them running (sails at exactly 2.0 rad/s, the wheel at
0.136), so the dynamic bodies joined to a motor start awake with the velocity they were saved with; asleep, the
mills would stand still until something hit them.

**Which way a motor turns.** PhysX's velTarget turns body A forwards and body B backwards: for all nine joints the
saved `(wA - wB) . axis` is `+velTarget` (0.500 and 1.997 for the roof and sails in `village-dinner`, 2.002 in
`village-rinok`, +0.136 for the wheel still spinning up towards +3), with the axis taken in world space from A's
pose. Jolt's hinge turns B forwards, so `PhysicsWorld` turns both hinge axes round: the hinge angle, the limits, the
spring target and the motor velocity of a `JointDesc` are those of body A relative to body B. (No shipped joint has
a limit or a spring; only the motors show the direction.)

The windmills keep turning: the sails hold 2.00 rad/s, the roofs 0.50, for the four minutes measured. The wheel
does not: its motor has 80 against an inertia of about 800,000 (the wheel and its six gondolas) and angular damping
0.05, so it coasts from 0.14 to rest in 20 s and falls asleep. Whether the original's wheel kept turning is not
known (`NxMotorDesc::freeSpin`, which the wheel has set, is not mapped either).

## Breaking joints

Jolt solves joints before contacts, so a joint holding a body that stands on the floor carries the body's whole
weight (a fixed joint to the world under a 700-unit brick: 114 impulse per step, its maxForce 500 allows 8), and
the loads inside a structure of joined bodies shift by many times the thresholds when it wakes. The thresholds are
a hundredth of a brick's weight; PhysX gave the weight to the contacts. Judged by load, awake
`piratos-ostrovok` loses 48 of its 55 joints within 10 s (28 in the first step), and when a hit wakes a sleeping
ship all of it lets go at once (rows "by load" above). Subtracting a rest load measured after settling did not
help: it moves by 4 to 15 times a threshold when the structure is touched.

So `PhysicsScene` breaks joints itself: a contact that begins at more than the bounce threshold (2 units per
second) strikes a force of effective mass * closing speed / fixed step, and every joint of both bodies with a
`maxForce` below that breaks (maxTorque is not used; kinematic and static bodies count as immovable, but for a sail, which weighs what its particles do together: a few grams). The world's own
thresholds are off (`JointBreaking::Impact`; `Load` is the world's way, kept for measuring). The step the hit
begins in still solves with the joints, so a ball bounces off a ship before it lets go. The bodies a broken joint
held are woken (the joint is gone by the time Jolt would wake its partner, and a piece left asleep would hang where
the joint left it). Not verified: that this matches how PhysX 2.x broke the joints.

## Overlaps

Some bodies were saved overlapping (jointed pieces never collided; a pile of logs was saved with its logs inside each
other). Woken, Jolt undoes an overlap by up to 0.2 per solver iteration, which threw bodies at up to 115 units per
second and pushed 3 of the 10,285 dynamic bodies into the 0.84 thick floor in the ball test. `PhysicsWorld` limits it
to 0.05, and with one position iteration it is slower still. The deepest overlap between two bodies in the shipped
levels is 0.69.

## Waking

Jolt wakes the sleeping bodies a moving body touches, and `PhysicsWorld` wakes whatever is joined to a body that
wakes. Two things escape that, and left bodies asleep in the air:

- A body falls asleep after 0.4 s in which it moved less than 6 cm, whether or not gravity is about to move it: if
  what held it up slides away in that window it stays in the air, asleep. A dynamic body that falls asleep with nothing
  within 15 cm under its lowest points and no joint holding it is woken again (three times at most, since a body that
  leans on something touches it away from its lowest points).
- The levels were saved with bricks a few centimetres above what holds them: of the 8,563 dynamic bodies with a
  surface within 15 cm under them, 320 hover 8 mm to 15 cm over it (175 of them more than 2 cm), which no contact
  reaches. When the support moves away, nothing wakes
  them. So `PhysicsScene` looks at every body that rests on something (once when the level is built, again whenever it
  falls asleep), and every tenth of a second at the sleeping ones without a live joint: one with nothing under it any
  more is woken. A brick knocked away from under a hovering one, a joint that breaks, and a support taken out of the
  world are all found this way (`piratos-bad-night` had a zombie 4 cm over a boat that stayed in the air for good).

`mb_physics_smoke` finds hanging bodies by waking everything that stood on something at the start, has nothing under
it, is asleep and has no live joint, and seeing what falls: 1 in 664 shots with the look switched off (the zombie),
0 of 205,512 bodies standing on something in 1,992 shots with it on.

## Path actors

The 35 sharks and birds with the path flag are kinematic bodies, and `PhysicsScene::step` moves them: before every fixed
step it asks `PathPlayer` for the pose at the end of that step and calls `setKinematicTarget`, so they reach it by the end
of the step and push whatever is in the way. (`PhysicsWorld::step` takes a function it calls before each fixed step, so
a call of several steps, the level viewer's, still gives each its own target.) What the paths are and how fast the
actors go is in `LEVEL_FORMAT.md`, "Path motion"; the speed is the one thing not checked against footage ([?],
`GAMEPLAY.md` section 13).

- They start on their path: the constructor teleports each body to its pose at time 0. The saved poses are a frame or
  two behind the paths, and one shark (`piratos-ostrovok`) was saved 3.2 units from its path: as a target of the first
  step that would be a kinematic velocity of 190 units a second.
- A kinematic body that moves wakes the sleeping dynamic body it touches, which is how a shark that swims into a brick
  starts it (the bodies all start asleep, see "The starting state"). With no ball, the 8 levels with path actors stand
  for 45 s and nothing but those actors moves (`mb_physics_smoke --seconds 45`).
- A destroyed shark has no body (`removeActor`) and the scene drives no more of it. Only a kinematic body is driven: an
  actor with the path flag that is not (there is none in the levels) stays where it is, with a warning.
- The linear velocity of a body is that of its centre of mass, not of the origin of its frame, so while a shark turns it
  is not the path's speed. `mb_path_smoke` compares poses, and `mb_physics_smoke` leaves the path actors out of its speed
  limit.
- Three paths have turns of 110 to 155 degrees with a radius of a few centimetres (`LEVEL_FORMAT.md`): the body is asked
  to turn that much in a step or two, and a loose brick next to its tail at that moment is thrown.

## The test ball

Sphere of radius 0.6, mass 500 (bricks weigh 51 at the median and 930 at the 90th percentile), the game material
`ballMaterial` (restitution 0.999), continuous collision, gravity. It is not the real ball (constant speed, step 4).
Space shoots one from (0, 1.2, -15) at 25 units per second; balls older than 20 s or below y = -30 are removed.
`--ball [x,degrees]` shoots one at t = 0 of a headless run (default: at the middle of the dynamic bodies).

The ball is 150 times heavier than the lightest bodies and bounces off them elastically, so it sends a mast or a
spar of a ship off at 50 units per second, more than the 0.54 thick floor per step. Swept collision on the dynamic
bodies keeps most of them from going through (16 bodies in 9 shots of 1,992 without it, 4 in 3 with it, all thin
light pieces of wrecked ships); the rest are a documented allowance of the smoke test (1% of the shots).

## The ball and the bat of the game

`GameSession` (`game/Gameplay.h`, ARCHITECTURE.md) plays a level in a `PhysicsScene` with its own ball and bat:

- **The ball** (`addGameBall`) is a sphere of 0.6 (`radius` says another: the Big Ball's 1.0 and the Weak Ball's 0.4) with `ballMaterial` (friction 0.5, restitution 0.999), continuous
  collision and no gravity, locked to the plane it starts on (y = 0.6) and free to turn only about the vertical, at up
  to PhysX's default 7 rad/s. Its mass is the material's density times its volume, 0.9 against the median brick's 51
  (`PhysicsWorld::mass()`, which a hit that breaks joints reads for the ball's size):
  a ball that light loses speed in every collision with a brick, which `GAMEPLAY.md` takes as the reason the original
  sets the speed back to 15 after each bounce (the rules do it after every fixed step). Friction at a wall turns the
  ball by 11 to 18 degrees on most bounces in the footage, both ways: it takes sideways speed off the ball and puts
  spin on it, and the spin turns the next bounce. (A ball with friction in a locked plane has one defect: it loses
  its sideways speed between two parallel walls until it goes straight up and down; the rules turn such a path out,
  `GameplayRules::minVerticalAngleDegrees`.)
- **The bat** (`addBat`) is a kinematic sphere of 3 that `setKinematicTarget` moves each step. The rules, not the
  physics, decide where a ball leaves it.
- **`removeActor`** takes a destroyed brick out of the world, joints and all. Jolt wakes nothing when a body goes, so
  the scene does what it does for a broken joint: the pieces that were joined to it wake, and the next step looks for
  sleepers with nothing under them (the look below, run at once instead of within a tenth of a second).
- **`contacts()`** are the contacts that began in the last step, the same list that breaks joints, for the rules.

## Cloth

The 22 sails of 9 levels are `ClothSet` (`game/Cloth.h`) over `PhysicsWorld::addCloth` (`engine/PhysicsCloth.cpp`), and what the original did, what the game does and why are in `GAMEPLAY.md` section 20. The physics of it:

- **A sail is one soft body** of Jolt: a grid of particles with springs along the grid, across its cells and across its folds. The segments of the level are the particles (their saved places, their sphere of 0.5 as the radius, their 5 grams as the mass); they have no bodies. The body stays at the origin and the particles are in world space, so that nothing can shift them.
- **The corners are pins**, not joints: Jolt's soft bodies take no constraint (its documentation: constraints cannot operate on soft bodies; a particle of inverse mass zero that the game moves is how one is tied to something else), so a sail's corner cannot be jointed to its yard whatever they weigh. The holders are yards, masts and boats of 1.07 to 67.99 in mass (82 of the 84; the other 2 do not move), not a ship's hull of 100,000, so it is not a ratio of masses either (`mb_cloth_smoke` asserts it; `GAMEPLAY.md` 20.1). A pinned particle is a kinematic one: each fixed step it is sent to `holder pose * where it is tied` (`PhysicsScene::beforeFixedStep`, so a call of several steps gives each its own target) and the springs follow. A pin lets go when the holder is destroyed (`removeActor`) and when a hit on the holder is as hard as the joint's `maxForce` (`breakByHit`, the rule of the joints, "Breaking joints"). A sail with no pin left falls.
- **One-way collision.** The cloth has a layer of its own that sees every body, and no body sees it (`ObjectLayerPairs`): a body that moves continuously casts against the faces of a soft body, and a face with a held particle (no mass) takes the whole momentum of the body into its other particles, which go to 10^7 in a step (found on a test ball that struck a collapsing ship's sail near a corner). So the particles collide with the bodies, one by one, with their radius, and a ball smaller than the gap goes between them, as it did the original's spheres; a ray hits the faces. A particle pushes nothing (`mInvMassScale2` 0): Jolt would wake a sleeping brick for the nothing it gave it, and a brick woken settles where the level saved it hanging (16 bodies of `po-volnam` moved 2 cm).
- **A sail does not touch its own ship** (the bodies joined to the ones that hold it, `clothIgnores`) while any pin holds; the saved sails lie against yards and masts. When the last pin goes it touches everything.
- **Sleep.** A sail the wind has settled goes to sleep (a few seconds), and costs nothing. It wakes when a pin goes somewhere else or a body that moves comes within a unit of its box (Jolt would do that last by itself if bodies saw the cloth); `mb_physics_cloth_test` (a settled sheet sleeps and wakes) and `mb_cloth_tests` (a sail sleeps and wakes when its yard moves) check both.
- **A guard.** A particle sent beyond 500 units by the solver puts the cloth back where it last was whole; the third time, the cloth is held where it is (`clothResets()`). `mb_cloth_smoke` asserts it never happens in the 22 sails.
- **Pushed.** `PhysicsWorld::pushCloth(cloth, velocity)` adds a velocity to every particle that is not pinned and wakes the cloth: how the Wind prize and the Earthquake move a sail as they move the loose bodies (`GAMEPLAY.md` 15.3, 20.2). The wind above only takes what is square to the sheet; a push takes any direction, but the pins and the springs hold a sheet hung from two corners in its own plane.
- **What a sail is to the rules of the scene.** For the joint-breaking rule ("Breaking joints") a sail weighs what its particles do together, a few grams (`massOf`), not the infinity of a kinematic actor: a ship that closes on a free sail breaks nothing. For the look at what holds a sleeping body up ("Waking" above: `PhysicsScene::supported()`) it is nothing, and so is a projectile passing under: the rays that look under a body ignore a sail's faces.
- **Cost.** A fixed step of the nine levels with sails is within 0.025 ms of what it was with the 453 kinematic spheres (0.15 % of a frame at 60 Hz): from 21 per cent less to twice as much, the large shares being in the quietest levels (`piratos-bul-bul` 0.019 to 0.038 ms, `Piratos-Zasada` 0.035 to 0.060; `GAMEPLAY.md` 20.7).

## Not done yet

The bat's lane (`PhysicsPlan::zones`) is not used: the rules take the bat's line as a number. The level's own actors are not
interpolated (a 120 Hz display shows each 60 Hz physics state twice; the balls, the bat, the prizes and the cannonballs and rockets are carried on by their
velocities, `PlayScene`). Step 5 is in: the sharks and birds on paths, the cannons, teleports, springs and volcanoes (see above and `GAMEPLAY.md` 17),
the weapons' projectiles (`GAMEPLAY.md` 18) and the sails (see "Cloth"). Several dynamic bodies are saved with
nothing under them and fall when woken (the pirate of `piratos-po-volnam` at x = 30, outside the board, falls 490
units; some boulders of `cowboys-samba` and others 11): they need a settle (none of those in the levels with path actors is near one). Not decoded: how the
original's `ballMaterial` and the board's restitution 1.0 combine on bricks (they bounce at 0.75 here); friction
uses only the dynamic coefficient. Awake, a few jointed models still pop when a hit wakes them (up to 0.69 in 0.1 s,
`cowboys-strelki`, the ships of `Piratos-Kartejniki`): that is overlap depenetration, not the joints.

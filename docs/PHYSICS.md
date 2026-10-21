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
| box, sphere, capsule (`halfHeight = height / 2`), convex (`hullPoints`) | the same shapes; planes, triangle meshes and hulls without points are counted and left out |
| body flags | all six frozen bits (the board 0xFF, the markers 0x7F) -> static; kinematic -> kinematic; else dynamic, frozen bits as locks, "disable gravity" as no gravity |
| mass, inertia, mass frame, damping, spin cap, sleep thresholds | as saved |
| friction, restitution, combine modes | the game material the **owning object** names (a cloth segment: its cloth's); none, or one the library lacks (3 bricks and 11 flickers), gives 0.5 / 0.5 / 0. `Shape::materialIndex` is a per-level handle and is not used |
| group 5 (markers of lights, shadow casters, backgrounds) | no shape, so no body (148 actors); they sit inside the board and collide with nothing |
| group 11 (one box per board at the player's end) | not in the simulation; kept as `PhysicsPlan::zones` for the bat and the lost-ball test (step 4). Trigger shapes (flags 0x7, the teleport sources) are zones too |
| cloth and its 431 segments | kinematic: they keep their saved poses until step 5 simulates them. The 84 joints that hold their corners (75 ball joints, 9 fixed) are planned but not built, or they would pin the ships to where the sails were saved |
| joint actors | resolved as `LEVEL_FORMAT.md` says (0 = world, else object id, else cloth segment); all 4,895 resolve. Fixed -> fixed, revolute -> hinge (limit, motor, spring as flagged), spherical -> ball. `collideConnected` is false |

## The starting state

The levels were saved from a running simulation. All 83 levels, 10 s, no ball; "moved" is dynamic and kinematic
bodies that ended more than 2 cm from where they began, "broke" joints (10,285 dynamic and 3,414 kinematic bodies,
4,811 joints). Reproduce with `mb_physics_smoke --measure`.

| Start | Joint breaking | Moved | Broke | Furthest / fastest |
|---|---|---|---|---|
| **A** asleep, zero velocity | by load | 0 | 0 | 0 |
| B awake, saved velocities | by load | 3,924 | 3,262 | 491 / 98 |
| C awake, zero velocity | by load | 3,747 | 3,267 | 491 / 98 |
| B, C with the joint frames as saved | by load | 4,014, 3,871 | 3,282, 3,269 | same |
| A | by impact | 0 | 0 | 0 |
| B | by impact | 2,338 | 138 | 491 / 98 |
| C | by impact | 2,183 | 88 | 491 / 98 |

Only A leaves every level as designed: awake, the overlaps the data saved (up to 1.4 deep) push bodies apart, the
saved velocities fling some off the board (the furthest 491 units is a body falling for the whole run), and by load
a third of all joints break. So every moving body starts asleep with its velocity dropped.

**Joint frames.** 459 of the 4,895 joints (9.4%) were saved stretched by more than 5 cm, 8 by more than a unit, the
worst 11.3 (`Piratos-Kartejniki`). With the stored frames a woken joint pulls its bodies together: in the first 0.1 s
awake, all levels together, 1,760 bodies move more than 2 cm and the furthest 1.36 units; with the second body's
frame rebuilt from the saved poses (`PhysicsOptions::rebuildJointFrames`) it is 1,550 and 0.69 (the rest is the
overlaps). Each joint then starts at rest and nothing snaps.

## Breaking joints

Jolt solves joints before contacts, so a joint holding a body that stands on the floor carries the body's whole
weight (a fixed joint to the world under a 700-unit brick: 114 impulse per step, its maxForce 500 allows 8), and
the loads inside a structure of joined bodies shift by many times the thresholds when it wakes. The thresholds are
a hundredth of a brick's weight; PhysX gave the weight to the contacts. Judged by load, awake
`piratos-ostrovok` loses 48 of its 55 joints within 10 s (28 in the first step), and when a hit wakes a sleeping
ship all of it lets go at once (rows "by load" above; with a ball and start A, 1,379 joints break against 357).
Subtracting a rest load measured after settling did not help: it moves by 4 to 15 times a threshold when the
structure is touched.

So `PhysicsScene` breaks joints itself: a contact that begins at more than the bounce threshold (2 units per
second) strikes a force of effective mass * closing speed / fixed step, and every joint of both bodies with a
`maxForce` below that breaks (maxTorque is not used). The world's own thresholds are off (`JointBreaking::Impact`;
`Load` is the world's way, kept for measuring). The step the hit begins in still solves with the joints, so a ball
bounces off a ship before it lets go.

## Overlaps

Some bodies were saved overlapping (jointed pieces never collided; a pile of logs was saved with its logs inside each
other). Woken, Jolt undoes an overlap by up to 0.2 per solver iteration, which threw bodies at up to 115 units per
second and pushed 3 of the 10,285 dynamic bodies into the 0.84 thick floor in the ball test (`mb_physics_smoke`
counts them: a log pile in `village-na-melnice`, one in `village-dinner`). `PhysicsWorld` limits it to 0.05: none
sink and the fastest body is 81. It is no cure for everything: eight boxes saved 90% inside each other on a floor
0.2 thick still push the lowest one through. The deepest overlap between two bodies in the shipped levels is 0.69.

## Waking

Jolt wakes the sleeping bodies a moving body touches, and `PhysicsWorld` wakes whatever is joined to a body that
wakes. But a body falls asleep after 0.4 s in which it moved less than 6 cm, whether or not gravity is about to
move it: if what held it up slides away in that window it stays in the air, asleep. One frog in `Village-Basketball`
did after the test ball hit its crate. A dynamic body that falls asleep with nothing within 15 cm under its lowest
points and no joint holding it is woken again (three times at most, since a body that leans on something touches it
away from its lowest points). `mb_physics_smoke` finds such bodies by waking everything that rests on nothing and
seeing what falls: 1 before the guard, 0 of the 9,109 bodies that stood on something after.

## The test ball

Sphere of radius 0.6, mass 500 (bricks weigh 51 at the median and 930 at the 90th percentile), the game material
`ballMaterial` (restitution 0.999), continuous collision, gravity. It is not the real ball (constant speed, step 4).
Space shoots one from (0, 1.2, -15) at 25 units per second; balls older than 20 s or below y = -30 are removed.
`--ball [x,degrees]` shoots one at t = 0 of a headless run (default: at the middle of the dynamic bodies).

## Not done yet

Step 4: the real ball and bat, bricks that die (the original's islands and ships are smashed by that, not by
physics: a ball cannot move a 34,000-unit island piece), the ball-lost and bat zones, prizes. Step 5: cloth and its
84 joints, moving sharks and trains on paths, cannons, teleports, springs, volcanoes. Not decoded: how the
original's `ballMaterial` and the board's restitution 1.0 combine on bricks (they bounce at 0.75 here).

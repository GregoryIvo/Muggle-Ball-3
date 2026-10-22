# Gameplay of the original, from footage

Research notes for step 4 (bat, ball, bricks, score, lives, level end) and step 5 (prizes).
Tags on claims: **[M]** measured from video, **[D]** read from the game's data files, **[I]** inferred, **[?]** not verified.
Times are video seconds (`currentTime`), frame step 1/30 s. Saved frames and numbers are in `reference/gameplay/` next to the repository, not in it (they are screenshots of other people's videos; index at the end).

## 0. Sources and method

Footage was played in the browser pane and never downloaded. Three recordings were used:

| id | video | window | notes |
|---|---|---|---|
| V1 | "Magic Ball 3 (2007) Full Playthrough - 2020", ChillCalex, youtube.com/watch?v=5WbGEeviosI, 3:05:23 | 1920x1080, **16:9** | all 83 levels. Chapters: pirates 0:50, witches 36:23, cowboys 1:40:26, knights 2:09:38, end 3:04:37 |
| V2 | "Magic Ball 3 (PC) - Gameplay \| No Commentary", youtube.com/watch?v=DpvwN-k35Eg, 24:04 | 1440x1080, **4:3** | same shape as our reference screenshots |
| V3 | "Magic Ball 3 #1 Level 1", James Games, youtube.com/watch?v=oSjPIw8W0zE, 0:52 | 16:9 | first level only |

I also found "Magic Ball 3 Full Gameplay" by Sba Stuff Gameplays (iwg6K0w6y40, 3:26:57) but did not need it.

**Measuring.** I stepped `<video>.currentTime` frame by frame and found the ball (white/blue sphere) as a bright blob. I refined it to sub-pixel on the 1080p source. Each screen position was turned into a board position by intersecting the camera ray with the plane y = 0.6, using the project camera (eye 0,42.7,-58.5, target 0,0,2.7, vertical FOV 32.8 degrees, horizontal FOV from the frame aspect).

**The camera matches the original [M].** The rectangle x -20..20, z -20..23 drawn with that camera lies on the water in V1 at 16:9 and in V2 at 4:3. See the `*camera-board-overlay*` frames, taken at V1 54.2, V2 75 and V2 262. The vertical FOV is fixed and the horizontal FOV follows the window aspect, so a 16:9 window shows more at the sides.

**Accuracy.** Ball position is good to about 0.1-0.3 u. Direction over a 0.5 s window is good to about 2 degrees. Several numbers depend on the assumed ball height y = 0.6, which mostly shifts z; if the ball were at y = 0, z in the far half would be about 1 u larger.

## 1. Board and walls

- **Floor, walls and camera in the data [D].** The 12 pirate boards (and level 1's `.json`) have the same floor and walls:
  - Floor boxes cover x ±22.3, z -20 .. 24.5.
  - The near edge of the floor is z = -20.
  - Side-wall inner faces are at x = ±20.4 and the far-wall inner face at z = 22.6. The far corners are cut diagonally; the boxes at (±16.2, 18.3) are the chamfers, and the rim is visibly an octagon.
  - **Ball centre limits are therefore x ±19.8 and z 22.0.**
- **Measured turning points of the ball centre [M]:**

  | turning point | video, time | ball centre |
  |---|---|---|
  | right wall | V1 137.0 | x = 19.31 |
  | left wall | V1 134.2 | x = -19.35 |
  | far wall | V2 204.77 | z = 20.58 (about 21.7 if the ball's centre is at y = 0) |

- **Walls are mirrors [M].** Reflections are specular: at the right wall the z velocity is unchanged (-4.9 to -4.6 u/s, V1 137.0), and at the far wall dz flips from +17.3 to -17.0 with dx kept (V2 204.8). One left-wall bounce showed dz changing from -1.8 to -4.5 u/s (V1 134.2), which may be noise.
- **Camera does not change [M].** The overlay lines up on levels 1 and 4 in V2 and on level 1 in V1.
- **Group 11 box [D, ?].** Its half extents (3,22,15) at (0,15,-17) read, with the stored rotation, as x ±22, y 0..30, z -20..-14. That is directly in front of the bat, but the bat's collision centre is at z about -21 (section 3), so the box is not the bat's centre line. Its purpose is unknown. It may be a "ball is in the bat zone" volume.

## 2. Ball

| property | value | evidence |
|---|---|---|
| radius | 0.6 [D]. On screen the ball is about 19.7 px across at the near edge at 960 px frame width | `Data/Objects/Balls/ball.xml` |
| **speed** | **15.0 u/s**, planar, constant. Windows: 15.2, 15.2, 14.7, median 15.04 over 151 windows (V1 56-67, level 1); 14.7 and 15.0 (V1 7877-7880, level 65); 15.6 (V2 196-198, level 3, other camera scale). Per-window scatter about ±3% | `measurements-ball-bat-tracking.txt` |
| in board widths | 15 / 40.8 = **0.37 widths/s**, so about 2.7 s wall to wall and 2.8 s near edge to far wall | [M] |
| speed per level | **No change with level number** [M]. Level 1 gives 15.0-15.2 and level 65 gives 14.7-15.0 | V1 54-67, V1 7877-7880 |
| after a bounce | **Speed is re-normalised to 15 u/s.** After the shark hit (68 degree direction change) it is 15.2 on both sides (V1 54.1). After an island hit it is 13.6-16 (V1 55.4-55.7). The ball material has restitution 0.999, static/dynamic friction 0.01/0.5, density 1 [D], while bricks are density 5-1000, so plain physics would not keep the speed constant [I] | |
| path shape | Straight segments. A 1 s free flight is within the scatter of a line (slopes 0.38, 0.43, 0.42, V1 54.2-55.2) | |
| vertical | Planar. No arcs or hops were seen. The ball rides on the surface with a soft shadow, and its apparent size is constant [M] | |
| spin | No spin effect on direction was seen. The ball texture rolls visually [I] | |
| bricks | **Billiard-like bounce and it pushes dynamic bricks.** The island sank and planks and sails flew after hits (V1 55.2-55.8, V1 20-48 in V3). After the bounce the speed is forced back to 15 [M/I] | |
| speed prizes | "Increase Ball Speed" took one ball from about 15.6 to 17.2 u/s (V2 196-198 before, V2 203.5-207 after; prize caught at about 200.9), so roughly **+10%** per catch. It is a red-aura prize and the HUD shows no icon. "Decrease Ball Speed" was not measured [M, ?] | |
| multiball | **More than one ball is normal.** V2 shows 3-4 balls on the board for long stretches. Hearts stay at 2 while balls are lost, so **a life is lost only when the last ball is lost** [I] | V2 40-64, hearts constant over 24 min |

## 3. Bat

- **Size.**
  - **Collision** sphere, radius 3 [D].
  - **Looks:** a crescent arc with a gear body.
  - **Width:** the default arc is about **5.4-5.6 u wide**, about 14% of the playfield width (V2 6.0, 81 px/u zoom). The sphere diameter is 6.
  - **Increase Bat Size:** one catch makes it about **+24%** wider, going from 75 px to 93 px of arc at the same zoom (V2 174-182). It then stays at about 93-95 px (V2 182-210). Decrease Bat Size looks smaller by a similar step (V2 1119) [M, weakly].
- **Movement.**
  - **Axis:** it moves only along x, at a fixed z.
  - **Centre z:** about **-21**, so it is drawn on the near edge line. That is -20.4 if the ball's centre is really at y = 0. This comes from the contact geometry: the ball centre is 3.6 u from the bat centre at contact, at z about -17.5.
  - **x range:** the bat centre was seen from **-15.9 to +16.1** over 308 samples (V2 5-476, 1.5 s apart), with no plateau. It probably reaches about ±16 [M, ?].
  - **Speed:** there is no visible speed limit. It follows the mouse with a short ease. The ramp at V1 60.9-61.1 goes 0.09, 0.3, 1.07, 1.4, 1.13, 0.48 u per frame, so about 42 u/s peak for a 4.5 u move. 1/15 s sampling shows moves of up to 4 u per frame, so at least 60 u/s [M].
  - **Camera follow:** none. The rim stays registered within 1 px between bat x = -15.4 and +8.3 (V2 34 vs 50, 14).
- **Appearance changes with weapons [M].**
  - **Cannon:** a barrel on the bat (V1 137.85 close-up).
  - **Laser:** blue emitters.
  - **Bazooka, machine gun:** launchers or barrels on the bat. Bullet spawn points are at x ±2.15, y 1.2, z +3.4 [D].
  - **Entry and exit:** the bat flies in on a spiral swoop at the start of a level or life (V1 52.2-52.9, 7876.4-7876.7), and flies off to a corner when it dies.
- **How the ball leaves the bat.**
  - **The incoming angle is ignored.** The outgoing direction depends on **where the ball touches the bat** (the offset d = ball x - bat x).
  - **It behaves like a deflection off the sphere surface.** The best simple model is dir = normalize(ball centre - bat centre) in the XZ plane, then speed 15.
  - **Measured offsets and outgoing angles** (angle from +z, positive towards +x):

    | video, time | offset d (u) | out-angle | angle if exit = normal (d / 3.6) |
    |---|---|---|---|
    | V1 56.54 | +0.97 | +11.2 | +15.6 |
    | V1 60.07 | +0.61 | +8.4 | +9.7 |
    | V1 61.80 | -0.38 | -3.4 | -6.1 |
    | V1 66.84 | +1.63 | +28.8 | +26.9 |
    | V1 72.40 | -0.80 | -8.8 | -12.8 |
    | V1 137.80 | +1.90 | +31.0 | +31.9 |

  - **Incoming angles in those hits:** -28, -55, +7, -31, +37, -29 degrees. A physical mirror reflection would give very different outputs, so it is not used. At 60.07 a ball arriving at -55 degrees went out at +8.4.
  - **A fit:** angle ≈ asin(d / R) with R between 3.6 and 4.5 stays within about 5 degrees. Small offsets give 8.5-13.8 degrees per unit, large offsets 16-18, so it is slightly convex. Hits while the bat was moving fast (66.84, 137.80) gave larger angles. The largest angle seen was 31 degrees. Edge hits (|d| near 3) were not captured [?].
  - **Spin and bat velocity:** none observed on the ball.

## 4. Life cycle

**Start of a level (V1 52-54, 3 samples).**
- The level cross-fades in at about 52.0. The bat swoops in from the lower left over about 0.7 s (V1 52.2-52.9).
- The ball is visible on the bat from about 52.4.
- The hearts appear one at a time at about 53.0 and 53.3.
- The player launches the ball (V1 53.72). See the `t52` and `t53` frames.
- **Starting hearts: 2.** Level 1 shows 2 hearts in V1, V2 and V3. In the first V1 frame (V1 53.0) only 1 shows while they fade in.

**Held ball [M].**
- The ball sits on the bat at z = -17.2..-17.3 (centre, plane y = 0.6), about 0.75 u left of the bat centre, and follows the bat instantly in x (V1 52.5-53.7, ball x tracks bat x with a fixed offset).
- It waits for the player. It was held for 1.4 s at V1 level 1 (V1 52.4-53.72), 2.5 s at level 2 (V1 146.0-148.5), and 0.25 s after a respawn (V1 7876.9-7877.1).
- **No auto-launch within 2.5 s.** The release is a mouse click [I]; the video does not show clicks.
- **Launch velocity [M]:**
  - It is about **(-3.0, +14.7)**, which is **11.5 degrees left of +z at 15 u/s**.
  - It is the same in three launches: V1 53.72, about 147.85, 7877.07.
  - It is independent of the ball's offset on the bat (-0.75, -1.3, +0.4).
  - Immediate speed, no ramp.

**Losing the ball [M].**
- The ball is lost when its centre goes below about **z = -23.5**. It was last visible at z = -23.2 (V1 7874.235) and gone one frame later.
- No y drop is involved.
- The ball is lost only when it passes the bat; there is no visible kill line before the bat (V1 7873.9-7874.4 close-ups).

**Timeline after a ball loss (V1 7874.25).**

| time | event |
|---|---|
| 7874.25 | ball removed |
| about 7874.7-7875.1 | bat leaves (it was at the right edge) |
| 7875.1-7876.3 | no bat |
| 7876.2 | hearts decrement from 5 to 4 |
| 7876.4-7876.7 | new bat swoops in from the lower left |
| 7876.8 | ball on the bat |
| 7877.07 | player launches |

Total about 2.6 s from loss to ready.

**Skull case (V1 8536.6).**
- The Skull is caught.
- The bat flies off to the lower left.
- The ball is still in flight, then vanishes about 1.4 s later (8537.9 visible, 8538.2 gone).
- Hearts drop at about 8538.5-8539.
- The bat re-enters at 8539.0.
- About 2.4 s in all.
- **All balls disappear with the bat** [M, one case].

**Hearts [M].**
- **Hearts are spare lives.** A heart is spent when the new bat appears, not when the ball is lost.
- So the game gives 3 balls: the one in play plus 2 hearts [I].
- The Life prize adds a heart. Counts seen: 2 up to about 2700 s, then 3, 4, 5, and 6 at V1 9252-9732. No cap was seen.
- Hearts persist across levels. They are not refilled at level start; the counts above only rise with Life prizes and fall at losses.

**Game over [?].**
- **No game over was seen.** Neither long run ever lost the last heart.
- Data: `Data/Menu/GameOver.xml` has a "Game Over" caption, "Your Score :" with the score in green, and `EnterName`/`NewHighScore` screens.
- **After level 83** the footage goes straight to the End story screens ("The End" button), at V1 11077-11090. No tally.

## 5. Bricks, damage, destruction, scoring

**Data fields (Data/Materials/materials.xml) [D].** Each brick material has `brickType`, `brickEnergy`, `brickScore`, `exploseType`, `exploseDelay` (ms), `explosePower`, sounds (hit/dead/drop/period) and physics. Counts: 213 materials are type 0, 17 type 4, 6 type 2 (system), 1 type 5 (tree). Energy 1 for 128, 2 for 22, 3 for 63, 4-5 for about 15. Scores run 4-777.

**Behaviour:**
1. **Hits.** A hit costs 1 energy. Energy-1 bricks die on the first hit [M]. The shark (E1, S91) was hit at V1 54.13 and its score arrived at 54.4 (V1 54.4).
2. **Delay.** After the last hit the object explodes after **`exploseDelay` ms**, then it is removed and the score is added at that moment. The shark: exploseDelay 250, score +91 at 54.3-54.4 and flash at 54.40. The island: exploseDelay 50, score +66 at 55.2-55.3 (V1 55.2, 55.3).
3. **Effect.** The effect is a short orange flash with particles and the material's "dead" sound (shark, V1 54.4-54.5). `exploseType` selects it: 3, 5, 6, 8 (dynamite), 9 (fire rocket). Exact looks per type were not catalogued [?]; the game's choice for each, from the particle files, is in 19.5. `explosePower` is 6.789 for live/people/prize objects and 1.0 for the rest; it is probably a blast impulse [I].
4. **Damage textures [D + M].** Materials with `brickType` 4 list one texture per energy level, worst first: `Box.jpg` (E4: Box-1D, -1C, -1B, -1A), `Skala.jpg` rock (E4: Skala--3 to --0), `Ground-2*`/`trava*` (E3: Ground-2-2, -1, -0). The last file is the intact look (see `data-damage-stage-textures-Box_Skala_Ground.png`). In footage black crack decals appear on ground and rock blocks as they are hit: V2 478-542 (a green tile with a black crack) and V2 566-598 (rock and ground blocks darkening). Which collisions count as hits for E > 1 was not isolated [?].
5. **Hit counting.** Bricks have `brickEnergy` hits; E3 bricks like the pirates and prizes were not shown being hit repeatedly [?].
6. **Score.** The score added is exactly the material's `brickScore` [M]: shark +91, island +66, then +132 at 55.4-55.5 and +360 at 55.7-55.8 (compound objects). **No combo or multiplier was seen** in about 10 events.
7. **Knocked-over but intact objects.** They are not scored; only destruction scores. Planks and sails stay afloat until hit [I]. What removes drifting pieces that nobody hits (e.g. V3 36-52) was not determined; the second study found that nothing does: weapons and blasts destroy loose pieces, falling and sinking do not ("Rules settled by the second study", 4).
8. **No prize actors in levels [D].** Across all 83 levels there are no `prize` actors. Prizes are produced in play: they fall from the position of a destroyed brick (V1 66.5-69.5, x constant), and bullets from Thunderbolt and Meteorite Rain carry `disablePrize` [D].
9. **Level special objects [D].** There are 452 `metall`, 128 `tree`, 116 `dynamite`, 63 `firework`, 26 `generatorbad`, 17 `generatorgood`, 21 `cannon`, 21 `spring`, 13 `volcano`, 22 `teleport` objects. Dynamite explosion radii are 5 (big) and 3 (small). Fire rockets fly up at 15 u/s for 2.5 s. They are for later steps.
10. **Level end.** The level ends when the **last destroyable brick is gone**. In V1 level 1 the last pirate was killed by a lightning bolt at 144.1. The board looked empty (anchors and the sun are scenery). A dark fade began at about 144.4 (144.12 by the fact-check, item 8 below) and level 2 was visible at 145.5 (V1 142.6-145.0, 140.5-148.5). Prizes caught during the fade still show their caption (Increase Bat Size at 144.7). In V2 level 1 ended the same way at about 89 (V2 86.5-108). Whether type 4 pieces (ground, animals) and kinematic bricks must also be destroyed was not known; the second study and its fact-check settled what is required, when the fade starts and the Next Level star as the usual end ("Rules settled by the second study", 2, 3, 7 and 8).
11. **No tally.** There is no score tally and no level-complete screen. The score does not change at the transition (V2 254.4-261, score 21862 before and after).

## 6. Score and HUD

- **Score text.** Bottom-left "POINTS : n", bottom-right "LEVEL : n" (strings `Scores` and `Level` in languages.xml). Levels run 1-83 in `levels.xml` order: Pirates 1-20, Village 21-46, Cowboys 47-63, Knights 64-83. V1 7873 shows level 65 = knight-castle, which fits.
- **Hearts.** A vertical column at the top-left, about 4% of the screen height each, with 5.7% pitch (V2 75: hearts at x 3%, y 3.7% and 9.3%).
- **Prize captions.** Green bookworm-font text at the bottom centre, about 2.2 s: visible at once on catch, strong for about 1 s, fading out. The text is the English prize name from languages.xml.
- **HUD icons (top-right) [M].** A vertical stack, one per active prize with a number. A **green ring is a blue-aura (good) prize and a red ring a red-aura (bad) prize**. The number is **seconds** (down 1 per second) for time prizes and **shots left** for weapons.
- **Prize catch scores [D + M].** Catching a prize adds that prize material's `brickScore`: Laser +360 (V2 61.65 and V2 61.6-61.7, "5187 to 5547") and Bonus +777 (V2 78.5, "6202 to 6979") are confirmed. Others read from the data are in section 7.
- **Scale.** Level 1 reaches about 6000-10000 points (V1 ends level 1 near 6278), level 83 about 752,000.

## 7. Prizes (for step 5)

**What they look like and how they move [M].**
- A glowing orb with a halo (blue aura = good, red = bad). The orb of Next Level is a golden sheriff star; the balloons seen at V1 143.5 are Increase Bat Size.
- It appears where a brick was destroyed.
- It then **moves straight toward the bat at about 9.1 u/s** (z -0.5, -9.7, -18.7 at V1 66.5, 67.5, 68.5; x drifts 11.2 to 10.0).
- It is caught when it touches the bat: score +brickScore, caption, effect (V2 61.0-63.8: the orb reaches the bat at 61.65).
- Missed prizes vanish below the near edge [I].

| prize file | caption (UI) | aura | catch score [D] | HUD | what was seen |
|---|---|---|---|---|---|
| bonus | Bonus Score | blue | 777 | none | +777 at once (V2 78.5) |
| life | Life | blue | 777 | hearts +1 | caption at V1 7874.0, hearts up to 6 |
| scull | Skull - Bat Killer | **red** | 666 | none | bat dies, life spent, all balls vanish (V1 8536.6) |
| incbat | Increase Bat Size | blue | 110 | none | about +24% bat width per catch (V2 177) |
| decbat | Decrease Bat Size | red | 180 | none | smaller bat (V2 1119) [weak] |
| speedup | Increase Ball Speed | **red** | 280 | none | about +10% (15.6 to 17.2 u/s, V2 200.9) |
| slowdown | Decrease Ball Speed | blue | 120 | none | not measured |
| bigball | Big Ball | blue | 180 | **20 s** | icon: ball (V2 87.6) |
| smallball | Weak Ball | red | 240 | **20 s** | red weight icon "100" (V2 361-376) |
| powerfull | Power Ball | blue | 170 | **20 s** | spiked red ball icon (V2 121.5) |
| commonball | Normal Ball | **red** | 120 | none | removes ball-changing effects: the Weak Ball icon vanished (V2 376.0 to 376.7) |
| explosiveball | Explosive Ball | blue | 150 | none | no icon; blast trigger radius 2.5 [D] |
| drunken | Drunken Ball | red | 220 | **30 s** (red icon, two green blobs, V2 107.2) | path effect not seen |
| tripple | Multiball | blue | 190 | none | several balls (V2 75) |
| magnit | Magnet | blue | 230 | **30 s** (horseshoe icon, V2 1067-1087) | effect not verified; sound `ballMagnited` |
| pingpong | Yo-Yo | blue | 140 | not seen (an icon `rubber.png` exists) | not seen; sound `ballRubbered` |
| wall | Wall | blue | 200 | **30 s** ("H" icon) | a rope across the near edge at z about -20 for 30 s (V2 688.0-717.8, measured by the rope's pixels) |
| night | Night | **red** | 280 | **10 s** (red moon icon) | the scene darkens (V3 9-12, V2 124) |
| gun | Machine Gun | blue | 300 | **12 shots** | twin bullets, speed 30 [D]; ammo drops about 4 per second while firing (V2 1075-1077) |
| cannon | Cannon | blue | 320 | **6 shots** | barrel on the bat; kernel velocity (0,3,40) [D] |
| bazuka | Bazooka | blue | 340 | **7 shots** | rockets, speed 52.5 [D] |
| lazer | Laser | blue | 360 | **5 shots** | yellow beam straight up from the bat (V2 63.0), speed 50 [D] |
| flash | Thunderbolt | blue | 210 | none | lightning from the top of the screen destroys a brick (V1 144.1 killed the last pirate; V2 52, 88); bullet falls at 50 u/s [D] |
| meteor | Meteorite Rain | blue | 4 | none | meteors, velocity (-10,-30,10) [D] |
| quake | Earthquake | blue | 190 | none | objects jitter and jump for about 3 s; camera does not shake (V2 179.8-184) |
| wind | Wind | blue | 100 | none | pieces appear to be blown sideways [weak, V2 382-391] |
| random | Surprise | blue | 299 | none | gives another prize (not catalogued) |
| endlevel | Next Level | blue | 120 | none | a golden sheriff-star orb; the level ends at once, whatever is left on the board: fade within 0.1 s, next level at 255.2, no bonus (V2 254.3-255.2; V1 11077; it ended 70 of V1's 83 levels) |

Ammo prizes stack with other icons (up to 3 seen at once, V1 84-92). A second prize of the same kind was not watched [?].

## 8. Camera

- **Fixed [M].** In all three recordings the camera never moved: the rim stays registered to within 1 px with the bat at far left, right and centre, and during Earthquake (V2 179.8-184; the only change was ±3 px vertically for 2 frames at 180.2-180.3, which may be a video artefact). No camera shake was seen on explosions.
- **SmartCamera [?].** The Options menu has a "Smart Camera" switch ("SmartCamera" string). Its effect was never visible, so either these players had it off or it does nothing visible here.

## 9. Other surprises and open questions

- **Prizes are frequent.** About one caught prize per 6 s in V2 (about 120 captions in 1230 s), and V1 level 1 had roughly 10 in 90 s. Without numbers on destroyed bricks I cannot give a drop rate [?]; the second study counted: about 16 caught prizes per level, 14.7 % of destroyed objects ("Rules settled by the second study", 6).
- **Players seldom lose a ball.** Over 3 hours V1 lost 1 ball plus 1 skull; V2 lost none. That says the game is forgiving or the players are good; no inference on difficulty.
- **A wall rope** keeps the ball in play for 30 s (V2 688-717.8); I did not capture the ball touching it [?].
- **Level timing.** Level 1 takes about 90 s in V1 and about 85 s in V2 for good players.
- **Not captured:** Yo-Yo, Magnet behaviour, Power/Big/Weak ball size effects, Drunken path, Wind mechanics, game over, edge-of-bat hits, hits-to-destroy for E3 bricks (pirates, prizes), far-wall at both ends.

## Corrections from an independent re-check (read these before section 10)

A second agent re-measured the most consequential claims from the same footage. Where it disagrees, it wins:

- **Losing the ball [M].** z < -23.5 is where a *vanish animation ends*, not the trigger. The ball starts shrinking
  when its centre passes the bat's line, z about -21 (just past the floor edge at z = -20), at V1 7874.10, and is
  gone 0.25 s later (7874.35); its on-screen size falls from 348 to 0 dark-blue pixels over 8 frames. Use: a ball
  whose centre goes below about z = -21 behind the bat is lost, then shrinks away over 0.25 s.
- **Walls are not pure mirrors [M].** Of 7 measured wall bounces, 2 were clean mirrors and 4 turned the ball by
  about 11-18 degrees, in both directions (V1 134.2, 92.7, 64.0; V2 9.97), so it is not a wall tilt: most likely
  the ball's spin and friction (BallMaterial friction 0.5/0.5, restitution 0.999). Let the physics bounce the ball
  off walls and bricks; then set the speed back to 15.
- **Hits per brick [?].** Only energy-1 bricks are verified to die on the first hit (shark, islands, planks,
  chests, palms). An energy-3 pirate survived several nearby passes (V1 57.9, 64.6, 66.2) and died at a ball
  contact at 132.1, which fits more than one hit but does not prove 3.
- **Loose pieces die without the ball [M].** A plank scored +34 at V1 132.9 while the ball was across the board.
  Pieces are destroyed (and scored) some other way: falling off the board, sinking, or hard impacts between
  bodies; the cause was not determined. Level completion may depend on those pieces. *Answered by the second
  study: the Laser beam and dynamite chains, and not falling or sinking ("Rules settled by the second study", 4).*
- **Scoring is exactly additive [M].** 55 score jumps in V1 level 1 (template OCR of the HUD every 1/30 s), each
  one brickScore or a prize catch, no multiplier. Level 1 holds 65 bricks worth 4,231; the final 6,278 means
  about 2,047 came from prizes. Single values seen: shark 91, island 66, palma1 110, palma2 120, chest 111,
  plank 34 or 37, shturval 28, flag 40, fparus 20, sail cloth 60, pirat-2 262, pirat-1 290.
- **Held ball and launch [M].** The held ball sits at z about -17.4, 0.25-0.8 left of the bat centre; after a
  respawn it is held about 0.4 s (player-triggered; the input is not visible). Launch about 10.7-11.2 degrees
  left of +z at about 15 u/s.
- **Bat hit law [M].** Five more hits fit "exit along the contact normal" (offset +2.07 gave +30.7 degrees; a V2
  hit at offset about 2.2 gave -37 measured vs -37 predicted); contact z follows the sphere: z = -21 +
  sqrt(3.6^2 - d^2). The exact law (asin(d/R) or about 15 degrees per unit) is still open.
- **Life timeline [M].** Heart drops exactly as the new bat starts entering (V1 7876.17-7876.20, bat pixels rise at
  7876.233), ball on the bat at 7876.73, launch about 7877.1. After a Skull catch every ball vanishes mid-field
  about 1.5 s later.
- **Ball material.** The ball uses BallMaterial (friction 0.5/0.5, restitution 0.999), not the 0.01/0.5 pair
  quoted in section 2 (that is the unused `ball.jpg` material).
- **Wall prize** lasts 30 s (caught at 73.97, countdown 16 at 88.2); its box sits at z about -20.0.

## Rules settled by the second study

A second pass over V1 (all 83 levels: the HUD score read as text every 1/30 s, a caption detector, a fade detector; 1,355 caught prizes and about 10,600 score events; the per-level table is `rules-V1-per-level-score-event-summary.txt`) answered what sections 5.7, 5.10 and 6 left open. A third, independent pass, the fact-check, looked again at five endings frame by frame (V1 levels 1, 2, 17, 23 and 30, and V2 level 1) and at 21 level starts. **Settled** below is what the passes agree on and what the game now does; items 7 and 8 are where the fact-check corrected the second pass, and the game follows it; **still open** is what neither could settle, kept as [?] with its evidence. The frames are in `reference/gameplay/`: `rules-V1-*` and `rules-V2-*` are the second pass, `rules-chk-*` the fact-check (names are shortened here to the part that tells them apart).

### Settled

1. **Level order is the order of `levels.xml`, all 83 [M].** The HUD shows 1 to 83 once each and in order, the board at the start of each run is the `levels.xml` entry of that number, the fact-check matched 21 levels across the four stages, and the stage borders agree to the second (village 36:23, cowboys 1:40:26, knights 2:09:38). The "LEVEL : 62" saloon of `original-saloon.jpg` is `cowboys-napadenie`, which V1 shows as LEVEL : 51 (about 274,000 points there; the screenshot's 768,915 are more than V1 has at level 83): it comes from another run, which is not known [?]. Nothing changes: the game plays `Campaign`'s order. Evidence: `rules-V1-hud-level-number-at-start-of-each-run-1to83`, `rules-V1-hud-level-scan-*`, `rules-V1-*boards-at-level-start*`, `rules-V1-t6422-cowboys-napadenie-is-LEVEL51-...`, `rules-chk-V1-order-*`.
2. **The Next Level star ends a level at once, with whatever is left [M].** The orb is a golden sheriff star with a halo (`Prizes/EndLevel.x`, material `endlevel`), not the balloons of Increase Bat Size. The catch scores +120, shows the caption "Next Level" and starts the fade within 0.1 s, with pirates, gift boxes and a spar still on the board (V1 level 2 at 209.70; level 18; V2 level 1 at 88.3). In V1 it ended **70 of the 83 levels**; 13 ended naturally (levels 1, 10, 17, 19, 23, 30, 31, 34, 45, 57, 58, 64, 65). The fade lasts 0.9 to 1.25 s, to about 35 % of the luminance, and the next level is cut in at full brightness (V1 145.17 to 145.29; V2 88.35 to 89.65); a prize that is already falling is still caught during it. That the star is a common random drop is not shown: in every ending sampled at least 90 % of the board was gone already, which a chance that grows as the bricks run out would look like too [?]. Evidence: `rules-V1-t207.4-209.15-L2-...`, `rules-V1-t209.15-L2-zoom-...`, `rules-chk-V1-t209.4-209.7-L2-...`, `rules-chk-V1-t2008-2017.3-L18-...`, `rules-V1-t10037.9-10039.6-L76-...`, `rules-V1-end-of-level-frames-L1-to-L32...` (8 sheets), `rules-V1-end-sequences-*`, `rules-chk-V2-t85.5-88.4-L1-...`.
3. **Metall, teleports and the cannon are not required [M].** At natural ends objects of these types stay on the board: four teleport pyramids and spiked Metall fence pieces when the fade starts in level 30, the whole wall of 83 dynamic Metall pieces in level 31, an `int_cannon` in level 10. Metall can be destroyed (E1, 50 points): in level 30 a dynamite chain takes a fence in one frame (+1917, 1,419 to 3,336) and single +50 events happen, by ball or by blast is not separated; most of it survives a level. Dynamite, fireworks, generators, springs, volcanoes, cloth and vibro were never seen at a natural end (sticks, gift boxes and a firework stood at star ends, which proves nothing). Decision: they are **not required**, since none of them is a brick; a ball can still destroy them for their points, and a level that will not end with one of them standing would show the decision wrong. *(Revised by section 15.1: the ball does not destroy cannons, springs, volcanoes, generators and metal, a blast, a cannonball or a weapon does; it still destroys dynamite and fireworks.)* Evidence: `rules-V1-t3581.9-L30-...`, `rules-chk-V1-t3581.7-L30-zoom-teleports-and-metall-fence-remain-...`, `rules-V1-t3462-3467-L30-...`, `rules-V1-t3467.5-3470-L30-...`, `rules-V1-t3788.6-3790.7-L31-...`, `rules-V1-t1120.1-L10-...`, `rules-V1-t2368.0-L21-...`.
4. **Loose pieces are destroyed by weapons and blasts, not by falling or sinking [M].** The Laser beam (a straight beam up from the bat) kills planks with the ball across the board (V1 59.25, 90.2 to 91.2) and a pirate (83.08); a dynamite chain takes a fence in one frame; no death from falling, sinking, drifting or a timer was found, and big ground blocks drift and tilt for 100 s without dying (V2 level 5). So in the game a body that falls off the board stops being a target and is taken out, **scores nothing** (`GameplayRules::fallScores` is off) and has no explosion. Whether the lightning bolts seen at most level ends are the Thunderbolt prize is still open, below. Evidence: `rules-V1-t58.6-59.3-L1-...`, `rules-V1-t59.2-59.37-L1-...`, `rules-V1-t90.2-91.2-L1-...`, `rules-V1-t82.7-83.14-L1-...`.
5. **Hits per brick are still not measured [?].** A pass at about 2 units does nothing (V1 131.4 to 132.0), a pirate wedged between two others for 0.3 s was not hurt, and the damage textures say energy = stages for type 4; the number of ball contacts that kill an E2 to E4 brick, and whether a weapon does more than 1, are open. The game keeps 1 damage per contact. Evidence: `rules-V1-t131.4-131.75-L1-...`, `rules-V1-t85.7-86.1-L1-...`.
6. **Prizes, for step 5 (recorded, nothing built) [M].**
   - About **14.7 % of destroyed objects** leave a prize that is caught (1,355 catches, about 16 a level; 13 to 14 % for the cheap E1 bricks, 4 to 6 % for objects worth 120 or more points, people and animals). Uncaught ones are not seen, so these are lower bounds. It is random, not every N-th: the gap between catches runs from 0 to 25 kills.
   - **Red prizes are ordinary drops from level 10 on.** No red-only value (+280 Increase Ball Speed, +220 Drunken, +240 Weak Ball, +666 Skull) occurs in levels 1 to 9 (about 100 catches); Increase Ball Speed starts at level 10 (94 events in levels 10 to 83, about 1.3 a level), Drunken at 23, Skull at 30 and 68, Weak Ball at 34; they are 9 % of the catches in levels 10 to 83. They do not come from the `generatorbad`s (levels 10 to 83 with and without one give 1.31 and 1.26 Increase Ball Speed a level). The generators were never seen to produce anything, and their gift boxes stood through whole levels (2, 21, 22, 76). A hard unlock level is not proven.
   - Weapon kills carry `disablePrize` [D] for the Thunderbolt and the meteors.
   - Evidence: `rules-V1-prize-captions-at-score-plus-280-events-increase-ball-speed`, `rules-V1-L2-generator-watch-frames-t148-186`, and the per-level table.

7. **Kinematic bricks are required [M for the ends, I for the rule]** (the second pass had them not required; the fact-check corrected it). The one ending read as a kinematic brick left alive, level 23, shows the opposite: the last object on the board was a kinematic `Ground-2-ground` block (the player had destroyed 83 of the 84 first), alive at 2629.40 to 2629.57, hit at 2629.57 and gone at 2629.60 (30 fps frames), the fade starting 0.05 to 0.1 s later; the +325 at 2629.18 started nothing. At the natural ends where the board can be seen no kinematic brick was left: level 65 had all 84 kinematic type 4 blocks and 12 `Water-brick` gone, level 17 all 60 and 12, and level 1's shark was killed at 54.4. Level 30's last two objects are dynamic crates (`Box.jpg`), which says nothing either way. So every brick and tree is required whatever its motion (dynamic, kinematic, bound to a path), and metall, teleports and cannons are not (item 3). It is the simpler reading, no ending shows a kinematic brick left standing, and V2 level 1's long wait ("Still open", 1) fits it if the shark, a kinematic brick, was out of sight [?]. In the 83 levels it makes 12,444 required objects where dynamic ones alone made 9,840. Evidence: `rules-chk-V1-t2629.40-2629.67-L23-...`, `rules-chk-V1-t2629.5-2630.45-L23-...`, `rules-chk-V1-t2629.0-2629.8-L23-...`, `rules-chk-V1-t2600-2629.3-L23-...`, `rules-chk-V1-t2480-L23-...`, `rules-chk-V1-t7846-8035-L65-...`, `rules-chk-V2-t1-7-L1-...`, `rules-chk-V2-t8-14-L1-...`, `rules-chk-V2-t44-74-L1-...`.
8. **The fade starts about 0.1 s after the last required object is gone, not a fixed 0.45 s after its score [M]** (0.45 s was the second pass's average of two cases). A plain brick, block or crate is gone with its score; a person or an animal pays its score and then shrinks away, and is gone 0.35 to 0.4 s later. Level 23: the last block hit at 2629.57 and gone at 2629.60, POINTS 225328 to 225372 (+44) between 2629.55 and 2629.60, the luminance falling from 2629.65. Level 30: the last crate +57 (13331 to 13388) between 3581.70 and 3581.77, the fade from 3581.83 to 3581.90, 0.1 s on. Level 1: `pirat1` paid +290 at about 143.75 (5878 to 6168 between 143.70 and 143.80); it stands at 143.80, shrinks at 143.90 and 144.00, is a dot at 144.07 and gone at 144.15, and the fade starts at 144.12 to 144.15. Level 17: the gorilla's +190 at about 1943.6, the body shrinking until about 1944.0, the fade from about 1943.97. Which objects shrink is not catalogued: a pirate and a gorilla are seen, and the game takes the materials of `exploseType` 5, the ones with no splinters to break into (people, animals, trees, fruit, barrels, the interactive objects: `OBJECTS.md` section 9); the rest vanish with the score [I]. No flash is seen on the pirate in those frames, so a shrinking object has none in the game [I]. In the game those objects are gone `GameplayRules::shrinkTime` = 0.35 s after their score, `LevelComplete` comes when the last required one is gone, and the fade starts 0.1 s after that. Evidence: `rules-chk-V1-t2629.40-2629.67-L23-...`, `rules-chk-V1-t2629.5-2630.45-L23-...`, `rules-chk-V1-t3581.4-3581.7-L30-...`, `rules-chk-V1-t3581.77-3581.97-L30-...`, `rules-chk-V1-t143.7-144.0-L1-...`, `rules-chk-V1-t144.07-144.32-L1-...`, `rules-chk-V1-t1943.45-1944.05-L17-gorilla-...`.

### What the game does with it (step 4)

| rule | in the game |
|---|---|
| required to end a level | `TargetRule::Bricks`, the default: every damageable `brick` and `tree`, whatever its motion (dynamic, kinematic: floor tiles, sharks, birds, or bound to a path). Not required: metall, teleports, cannons, and the rest of item 3. `BrickType` (H-type) is refuted and kept for comparing and for tests |
| safety net | the look at what the ball can reach and the stall rule stay (section 12 says how often they fire) |
| natural end | `LevelComplete` as the last required object is gone: with its score, or for a person or an animal (`exploseType` 5) 0.35 s after it, shrinking away meanwhile; the picture holds **0.1 s**, fades out in 0.8 s, and the next level comes up out of the dark |
| a piece off the board | not a target any more, taken out (`ActorFell`), no points, no explosion |
| level order | `levels.xml`, as `Campaign` has it |
| Next Level, prizes | step 5, section 15; `GameSession::forceLevelComplete()` is what the star calls |

### Still open [?]

1. **Natural ends that do not follow the timing of item 8.** Levels 10, 19 and 31 end 1.1 to 2.0 s after their last readable score (in level 31 five lightning strikes hit the Metall wall without a score meanwhile), levels 34, 57 and 65 0 to 0.3 s after it: not explained. V2 level 1 does not end when its board is empty: the last pirate dies at about 86.2 (+262, 7053 to 7315 by 86.5) and no brick is left in sight, yet no fade follows until the star is caught at 88.3 (+120, the caption from 88.55, the next level at 89.65), where item 8 would have ended it at about 86.6. A counted object may have been out of sight: the kinematic shark, whose fin is seen at 14 s and not after about 50 s, is one under item 7. A star in flight does not block an end: in level 23 one was still falling when the natural fade began (2629.4 to 2629.8). Evidence: `rules-chk-V2-t85.5-88.4-L1-...`, `rules-chk-V2-t87.2-L1-...`, `rules-chk-V2-t77-90-L1-...`, `rules-chk-V2-t8-14-L1-...`, `rules-chk-V2-t44-74-L1-...`, `rules-chk-V1-t2629.0-2629.8-L23-...`.
2. **The lightning bolts are not shown to be the Thunderbolt prize.** V1 level 1 has no Thunderbolt caption from 54 to 144.8 (scanned at 2 s steps or finer), yet bolts strike at 133.0, 139.2, 142.0 and 143.9, all in its last 11 s; V2 level 1 has none from 0 to 90 and a bolt about every 2 s from 81.0 to 89.0; level 30 none in the 30 s before the bolt at 3581.77. In V1 level 2 one caption (199.5) is followed by bolts at 200 and 206.5, so one catch may give several strikes. The score comes first and the bolt is drawn 1 to 2 frames later (+34 at 132.90, bolt first visible at 132.94); frame differencing finds no plank vanishing at that +34, and the visible strike at 133.0 to 133.4 scored nothing. Who throws the bolts is unknown, possibly a help of the game when few bricks are left [?]; in V2 at 81.0 to 81.5 a prize orb appears exactly where a bolt struck, which does not fit `disablePrize` for bolt kills. The Laser beam, dynamite chains and the absence of any kill by falling stand. Evidence: `rules-chk-V1-t132.84-132.97-L1-...`, `rules-chk-V1-t132.2-133.2-L1-...`, `rules-chk-V1-t139.2-142.3-L1-...`, `rules-chk-V1-t54-100-L1-caption-scan-...`, `rules-chk-V1-t186-209-L2-...`, `rules-chk-V1-t3550-3582-L30-...`.

## 10. Constants for step 4 (summary)

| constant | value |
|---|---|
| ball radius / speed | 0.6 / **15.0** u/s, y fixed at 0.6, speed renormalised after every collision |
| walls | side inner faces ±20.4 in the footage's overlay (centre limit ±19.8); the converted boards have them at ±20.0 at the ball's height (centre limit ±19.4, as the measured turning points 19.31 and -19.35), far wall 22.6; the physics bounces (not pure mirrors, see Corrections) |
| ball lost | centre passes the bat line, z < about -21 (then a 0.25 s shrink); see Corrections |
| bat | x only; collision sphere r 3, centre z about -21; centre x clamp about ±16 [?]; no speed cap |
| exit after bat hit | direction = normalize(ball - bat centre) in XZ, speed 15 (R 3.6-4.5 fits; ignore incoming angle) |
| held ball | z = bat z + 3.7 (about -17.3), x = bat x - 0.75; follows the bat |
| launch | click; about 11 degrees left of +z at 15 u/s (10.7-11.2 measured, see Corrections; first read 11.5); no auto-launch |
| lives | 2 hearts = spare lives; a heart is spent when the new bat appears; Life +1; no cap seen |
| loss to ready | about 2.6 s from the ball passing the bat line: bat flies off at +0.6 s, heart -1 and the new bat starts to swoop in at +2.1 s (see Corrections), ball on the bat at +2.63 s |
| brick | energy hits; damage texture index = energy-1 for multi-texture materials; explode `exploseDelay` ms after the last hit; score +brickScore at the explosion; a person or an animal (`exploseType` 5) then shrinks away and is gone 0.35 s later, the others at once |
| level order | exactly the order of `levels.xml`, all 83 [M] ("Rules settled by the second study", 1) |
| required to end a level | every damageable `brick` and `tree`, whatever its motion (items 7 and 3); not metall, teleports, cannons, and, by decision, not dynamite, fireworks, generators, springs, volcanoes, cloth, vibros. H-type (type 0 only) is refuted |
| level end | the last required object gone (a person or an animal 0.35 s after its score, once shrunk away): the picture holds 0.1 s (item 8), fades out in about 1 s (to 35 % of the luminance, then a hard cut), the next level follows; no tally. The `endlevel` star (+120, "Next Level") does it within 0.1 s, with the board not cleared, and ended 70 of 83 levels |
| loose pieces | destroyed by the Laser beam, blasts (dynamite chains) and, perhaps, lightning bolts; **not** by falling or sinking: a body off the board is just gone, scores nothing and does not count |
| prize | falls straight toward the bat at about 9.1 u/s; catch = +brickScore of the prize material; caption about 2.2 s |
| prize drop | about 14.7 % of destroyed objects (13-14 % for E1 bricks, 4-6 % for objects of 120+ points), random; red prizes from level 10 on (Drunken from 23, Skull 30 and 68, Weak Ball from 34); weapon kills carry `disablePrize` [D] |

## 11. Evidence index (`reference/gameplay/`)

- Camera overlays: `chillcalex-...-t54.2-camera-board-overlay.jpg`, `nocommentary-...-t75-camera-board-overlay-4x3-level1.jpg`, `...-t262-...-level4.jpg`.
- Launch and bat: `...-t54.0-ball-closeup.jpg`, `...-t137.85-bat-closeup-ball-contact.jpg`, `nocommentary-...-t6-normal-bat-closeup.jpg`, `...-t174-210-bat-width-after-increase-bat-size.jpg`.
- Bricks and score: `...-t53.5-56.9-score-timeline-hud.jpg`, `...-t54.1-54.5-shark-hit-and-explosion.jpg`, `...-t55.0-55.8-island-hit-and-sinking.jpg`, `data-damage-stage-textures-Box_Skala_Ground.png`, `nocommentary-...-t478-542-...cracked...jpg`, `...-t566-598-crack-damage-decals...jpg`.
- Lives: `...-t7873-7878-ball-lost-bat-respawn-overview.jpg`, `...-t7874.3-7878.0-lost-ball-respawn-hearts-5-to-4.jpg`, `...-t7876.3-7879.4-new-bat-flies-in-ball-on-bat.jpg`, `...-t8535.5-8539.5-skull-bat-killer-life-lost.jpg`.
- Level flow: `...-t52-level1-fade-in-bat-flies-in.jpg`, `...-t145.5-149.5-level2-start-ball-waits-on-bat.jpg`, `...-t142.6-145.0-level-end-fade.jpg`, `...-t11060-11110-game-ending-after-level83.jpg`.
- Prizes and HUD: the `...prize-captions...`, `...hud-*...`, `...magnet-hud-timer-30s.jpg`, `...laser-prize-score-plus360.jpg`, `...bonus-score-plus777.jpg`, `...earthquake-no-camera-shake.jpg`, `...wind-prize.jpg` frames.
- Numbers: `measurements-ball-bat-tracking.txt` (speeds, launches, hit table, wall and far-wall points, lost-ball point, prize fall).
- The second study (`rules-V1-*`, `rules-V2-*`) and its fact-check (`rules-chk-*`), by the item of "Rules settled by the second study" they belong to:
  - Order and HUD: `rules-V1-hud-level-number-at-start-of-each-run-1to83.jpg`, `rules-V1-hud-level-scan-*`, `rules-V1-*boards-at-level-start*`, `rules-V1-cowboys-*-hires-start.jpg`, `rules-V1-t6422-cowboys-napadenie-is-LEVEL51-...jpg`, `rules-chk-V1-order-*`.
  - Level ends: `rules-V1-end-of-level-frames-L1-to-L32...` (8 sheets), `rules-V1-end-sequences-*`, the single sequences `rules-V1-t*-L2/L7/L9/L10/L11/L12/L15/L17/L19/L21/L22/L23/L27/L29/L30/L31/L57/L76-...`, `rules-chk-V1-t*-L1/L2/L17/L18/L23/L30/L65-...`, `rules-chk-V2-*-L1-...`.
  - Metall and blasts: `rules-V1-t3462-3467-L30-...`, `rules-V1-t3467.5-3470-L30-...`, `rules-V1-t2368.0-L21-...`, `rules-V1-t3788.6-3790.7-L31-...`.
  - Loose pieces and bolts: `rules-V1-t58.6-59.3-L1-...`, `rules-V1-t59.2-59.37-L1-...`, `rules-V1-t90.2-91.2-L1-...`, `rules-V1-t82.7-83.14-L1-...`, `rules-V1-t132.*-L1-...`, `rules-chk-V1-t132.*-L1-...`, `rules-chk-V1-t54-100-L1-caption-scan-...`.
  - Hits: `rules-V1-t131.*-L1-...`, `rules-V1-t85.7-86.1-L1-...`, `rules-V1-t86.1-86.5-L1-...`.
  - Prizes: `rules-V1-prize-captions-at-score-plus-280-events-increase-ball-speed.jpg`, `rules-V1-L2-generator-watch-frames-t148-186.jpg`.
  - Numbers: `rules-V1-per-level-score-event-summary.txt` (83 levels: fade time, end type, event and catch counts, last events, catch values).

## 12. As built in the game (step 4)

What the game does with the notes above where they leave room. `[I]` is a choice made from the footage rather than a measurement; the
numbers are the constants of `GameplayRules` (the rules) and of `PlayScene` and `PlaySession` (the picture).

**The life cycle on screen.**
- A level comes up out of the dark in 0.6 s and the bat swoops in over 0.7 s from beyond the lower left, hopping and spinning once and a
  half, to land under the cursor; the held ball grows on it as it lands (V1 52.2-52.9, 7876.50-7876.73) [I].
- A ball that is lost carries on along its path and shrinks away in 0.25 s ("Corrections") [M]. 0.6 s later the bat flies off for 0.4 s
  towards the corner on its side of the board, spinning and getting smaller (it left to the right from the right edge at 7874.5, and to the
  lower left from the left at 8537.0) [M for the side, I for the path]; the heart is spent and the new bat swoops in as before (0.53 s).
- When the last target is gone the picture holds for 0.1 s ("Rules settled by the second study", 8: it was 0.45 s, and 0.3 s before that),
  fades to black in 0.8 s with the HUD left in sight, and the next level (or the end of the game) follows at once: no tally and no Loading form
  (V1 144.4-145.5) [M for the hold, I for the fade: the footage fades to 35 % of the luminance in about a second and cuts]. When the game is
  lost the picture holds 0.45 s, which is a guess: the footage has no game over [?]. Neither the fade nor the pause can be interrupted: Escape
  does nothing while a level is fading out, because leaving from the pause would lose how it ended (a win never recorded, a game over that never
  reaches the name entry), and the click or key that ends a pause is not given to the level, or it would launch the ball.
- The ball held on the bat goes on with the bat between the session's steps, as the bat does, so that it does not shimmer against it at 120 Hz.

**Bricks and the ball.**
- A brick that is hit shows the texture of its damage state (`damageTextureIndex`): a crate (Box.jpg) goes from whole to cracked, smashed and
  black as its energy falls from 4 to 1 [D]; one that explodes is gone at that moment, with an orange flash (`Oreols/02.png`, 0.4 s, swelling and
  fading) and twelve sparks thrown up and out [I: the footage's flash is orange and short, V1 54.40; which particle file an `exploseType` would
  pick is not known, so every brick has this one].
- A person or an animal (a material of `exploseType` 5, `shrinksAway`) pays its score as it is destroyed and then shrinks away, in a straight line
  to nothing about the centre of its body, over `GameplayRules::shrinkTime` = 0.35 s, and nothing hurts it meanwhile; it is a target until it is
  gone, so the level ends after the shrink. It has no flash: the pirate's frames (V1 143.70-144.15) show none, and show it at about half its size
  0.15 s after the score, small at 0.25 s and a dot at 0.32 s, which the line follows (0.6, 0.3 and 0.1) [I for the curve and for the objects of
  the class other than a pirate and a gorilla]. The picture asks the rules for it (`ActorStatus::shrink`) and scales the actor (`LevelScene::setScale`).
- The ball rolls, turned about the horizontal axis across its path by the distance over its radius, and has a soft blob of shadow on the floor
  [I; the footage has a "soft shadow" under it, section 2].
- The sounds are in `AUDIO.md`; `ballHitBat`, `bat_dead` and `endlevel` are names in `sounds.xml` that no material uses, so which event plays them is
  inferred.

**A level that cannot be finished.** The level ends with the required objects of the second study and its fact-check (every brick and tree, whatever
its motion: items 3 and 7); a required object that nothing can touch would make it impossible. Two things are done about it, both in `GameSession`
(`SessionOptions::ignoreUnreachable`, `GameplayRules::stallSeconds`); neither is in the original as far as the footage shows, and the footage cannot
tell whether it had a help of its own (the lightning bolts, "Still open", 2):
- A target the ball cannot get at is not one (`BallReach`): the plane the ball moves on is flood-filled from the bat's end of the board around the
  walls, platforms and pinned buildings, a ball's radius from each; a target at the ball's height with no such ground near it (beyond a wall or the
  board's edge, in a slot narrower than the ball) is out, and so is one above the ball that is pinned, on a path, has no gravity or has no such
  ground under it (on a platform or a wall top). The look is made at the start and twice a second for the loose ones. In the real levels it finds 151
  targets (of 12,444) in 43 of the 83 (15 in knight-castle, 12 in knight-river, 9 in knight-garden and in knight-nazad-k-mame, the others by ones and twos).
- When balls have been in flight for 90 s without touching a brick and at most 12 targets (or a quarter of those the level began with) are left,
  they are let go. This is for what the look cannot see: a mast held in the air by joints with nothing left to hold them, a cactus that settled
  10 cm above the ball's reach, a bird that hangs where the physics left it [?]. It is generous (a player could stall a level out), but only with
  few targets left and after a minute and a half.

How many levels the autopilot finishes in 900 s of game time (`mb_gameplay_smoke --seconds 900`, which ends the levels as the game does; it
launches at once and keeps the bat under the ball with no aim, with 99 hearts so that it plays on, and aiming at the nearest brick or among the
four nearest cleared about the same number), now that the kinematic bricks are required (12,444 required objects in the 83 levels, 9,840 before): **60 of 83**
(it was 63), the median level in 7 minutes. 46 ended by destroying every target and **14 by the stall rule** (it was 47 and 16), which let go of 1 to
6 targets (29 in all, a single one in 9 of the 14). Only 8 of those 14 needed it to finish within the time (the 6 others finish without it, 6 to 79 s
later; it was 6 of 16), and the look at what the ball can reach lets 3 more levels finish (52 with the look alone, `--no-stall`): without either safety net
(`--strict`) the autopilot finishes 49 (it was 55), and the 11 it loses stand still with 1 to 6 targets left that nothing touched for 90 s [?]. So
the stall rule fires in a quarter of the levels the bot finishes and decides the outcome in 8 of 83: still needed, and it is the aimless bot that
stalls; a person aims, and has the weapons. The 23 levels it does not finish (14 knights', 8 village's and 1 pirates') are slow, not stuck: 19 were
still destroying bricks in the last 30 s of the run, the other 4 (knight-labirint, knight-nazad-k-mame, knight-za-princessoi and village-logovo-vedmi,
with 1 to 8 targets left) had stood still for 35 to 81 s, so that the stall rule was about to let go of them; most of the 23 have 100 to 250 targets.
A person with the prizes will be faster. With step 5's special objects (section 17: the ball no longer destroys metal, cannons, springs and the
like) the same run finishes **62 of 83**, 39 by destroying every target and 23 by the stall rule (it was 46 and 14: the metal stays, and the autopilot
leaves more levels with a few targets that nothing reaches, which the stall rule lets go). `village-apple`, whose two fences open only through a
crate of `Box.jpg` and the firework crate that falls into the gap (17.1), is slow for it, not shut: its gaps are open at 40 to 51 s and 265 to 306 s and
100 of 118 targets are left at 900 s; the rest vary with the paths the ball takes. With the prizes as well (section 15) the run finishes
**68 of 83**, 53 by destroying every target and 15 by the stall rule; the Next Level star ended 22 of them. Of the 15 left open, 13 were boards with 8
to 121 targets left and 2 (`knight-nazad-k-mame`, `Piratos-Ygoshenia`) had one.
With the weapons as well (section 18: the autopilot clicks every frame while it holds a weapon and something it can hurt is the first thing in the line of a barrel, which the
shot interval holds to four or five shots a second) the run finishes **75 of 83**, 60 by destroying every target and 15 by the stall rule (it was 68, 53 and 15); the Next Level
star ended 31 of them (it was 22). The bat caught 135 weapon prizes and 27 of the Thunderbolt and the Meteorite Rain, and the autopilot fired 463 Machine Gun, 228 Cannon, 375 Bazooka
and 15 Laser shots, so that 129 bolts and 5 meteors struck. 2,054 prizes dropped, 707 were caught, and the look at what the ball can reach put 160 targets out of play (it was 180). Of the 8 left open, 6 were boards with 44 to 91 targets left (`knight-fruktovie-derevia`,
`knight-na-kacheli`, `Piratos-Logovo`, `Village-Basketball`, `Village-Cafe`, `village-domino`) and 2 had 3 and 6 (`village-school`, `knight-na-mostike`). The smoke's check that a
ball in flight moves 0.3 units in a window was widened from 2 s to 4 s for it: in `Piratos-Ygoshenia` a ball in a pocket of ground blocks of three energy was freed twice, a second
apart, into the same pocket before the blocks it hurt gave way.

## 13. Moving sharks and birds (step 5)

35 objects in 8 levels move along paths: the 19 sharks of `piratos-po-krugy` (two rings, circling), the 10 birds of
`cowboys-samba` (a dance round one loop), and one shark in each of `piratos-ostrovok`, `piratos-birthday`,
`piratos-dva-doma`, `piratos-ribalka`, `Piratos-Zagoraem` and `piratos-zoloto-gorillas`. What the data says about the
paths, the curve and the speed is in `LEVEL_FORMAT.md`, "Path motion" ([D] there); this is what the game does with it.

| rule | in the game |
|---|---|
| what moves | the objects with the path flag, as kinematic bodies: each fixed step `PhysicsScene` sets the pose the path has them at, at the end of the step, so they push the ball and any loose brick in their way, like the bat |
| how fast | the track's speed, in units a *millisecond*: **5 units/s** for the sharks, **6** for the birds of `cowboys-samba`, **7** for the shark of `piratos-dva-doma`. **[?] Not measured on footage.** From the data: the saved poses lie 17 ms of travel (34 in `piratos-ribalka`) behind their keys, a frame at 60 Hz **[I]**. Whoever measures a shark on the footage should compare: a shark of `piratos-ostrovok` covers its 66 unit loop in 13 s, and 40 units (the board's width) in 8 s |
| the path | a closed curve through its points (Catmull-Rom), walked at a steady speed along it, for ever; the object faces along the curve and leans with the points' ups. A lap takes 6.6 to 20 s |
| start | where the key puts it, in the right place along the path: the objects of one level share a loop with an even spacing (about 10, 8 and 5 units apart) |
| hits | as for any brick: a shark has 1 energy (91 points), a small one 97 and a bird 120, both 2. The ball meets a moving body with its speed in the contact, and bounces off it |
| destroyed | removed with its body; it stops being drawn and driven |
| the end of a level | required, like every brick whatever its motion (`TargetRule::Bricks`, settled item 7): a level ends only when its last shark or bird is gone too [I: the footage shows the shark of level 1 destroyed early; `piratos-po-krugy` ended by the Next Level star] |

Open: the speed above; whether the original's sharks turn the way the curve does (the paths of `piratos-ribalka`, `piratos-birthday` and
`Piratos-Zagoraem` turn by 110 to 155 degrees with a radius of 0.02 to 0.15 units: a shark that pivots on the spot at
the end of its run, and swings its tail past at over 100 units a second [I]); whether a hit changes a shark's course (nothing in the data says so, here it swims on, and a bounce off
it adds its velocity to the ball's, whose speed the rules then set back to 15); and the shark of `piratos-ostrovok`,
whose key is 3.2 units off its path (it starts on the path).

## 14. Special objects and prizes (third study, from footage)

Added after the second study. Same tags: **[M]** measured from video, **[D]** read from the data files, **[I]** inferred, **[?]** not verified. Times are video seconds. V1 = ChillCalex 5WbGEeviosI (1080p, 30 fps). V2 = DpvwN-k35Eg (4:3, **60 fps**). Frames are `reference/gameplay/special-*.jpg`, cited as **F1..F49**; the list is at the end of this section.

**Method.** Frames were read with `captureStream` plus `MediaStreamTrackProcessor` at 0.25-0.75x speed. Ball, bullet and prize positions were taken from the pixels and turned into board coordinates with the project camera, as in section 0. V2's frame counter drops frames, so V2 speeds use video-time differences, or ratios only. Heights used for the plane intersection were 0.6 for the ball and 1.0-1.2 for prizes and bullets, so absolute speeds are good to about 10 %. Wherever the original's executable would have decided something that footage cannot show (a click, a random roll), the tag says so.

### 14.1 Quick rules for the game (what to build)

| thing | rule | tag |
|---|---|---|
| prize fall | straight along -z (x constant) at **about 10 u/s** from where the brick died; **not homing on the bat**; several can fall at once; a missed one fizzles at the near edge (z about -21) in a small blue sparkle | M |
| catch | the orb touches the bat; measured: caught at |dx| 1.8 u, missed at 9 u and 26 u. Build: catch if the orb overlaps the bat sphere (r 3) plus the orb half-size 1, so |dx| about 4 u | M for the 3 cases, I for the 4 |
| weapons | shot count on the HUD drops by 1 **at the shot**; one shot per click; cannon 6, bazooka 7, machine gun 12 (two bullets per shot), laser 5 | M |
| timers on the HUD (seconds, 1 per second) | Big Ball 20, Weak Ball 20, Power Ball 20, Drunken 30, Magnet 30, Night 10 (Wall 30 from section 7) | M |
| speed prizes | Increase Ball Speed x1.10, Decrease Ball Speed x0.91 per catch, no timer and no icon | M |
| ball size | Big Ball about x1.6 diameter (r about 1.0), Weak Ball about x0.63 (r about 0.4) | M |
| interactive cannon | fires when hit by the ball, debris or a cannonball; along its local +x axis; about 30 u/s | M |
| teleport | the ball vanishes at the source, is gone about 0.2 s, comes out at the destination, drifts for about 0.2 s, then leaves toward the bat side at about 15-17 u/s | M |
| dynamite | blast trigger radius about 5 u (between 4.0 and 6.9 u); chains in about 20-30 ms per link | M, D |
| firework | after a hit it fires a rocket about 0.7 s later; the rocket rises at about 15.6 u/s and bursts after about 1.2 s; no score; the crate stays | M |
| path movers | speed in u/s = binding `param` x 1000; closed loops in the order of the path points | D checked by M |

### 14.2 Level objects

**Interactive cannon** (`pushka`, level type `cannon`, 21 in levels). Watched in V1 level 51 `cowboys-napadenie` (6420-6448): three cannons at (-5.3,-10.2), (-0.4,-9.4), (3.9,-10.1), all turned toward +z. Frames F15, F16, F17, F18.
- **Trigger [M].** It never fired on its own in 28 s. Five shots at 6435.00 (middle), 6435.95 (left), 6440.63 (left), 6446.51 (right), 6447.21 (middle). In four of them the ball was about 1.5-2 u from the cannon at the shot (the dark ball appears within one frame, 0.03 s, of the closest approach, F15); the fifth followed a flying piece striking the cannon (F18, a pink ring at the muzzle). So any collision seems to fire it [I for debris].
- **Direction [M, D].** It fires along the cannon's local +x axis (column 0 of the stored rotation). Measured heading versus axis: +1 vs +1 degrees, -10 vs -10, +11.5 vs +12.8.
- **Projectile [M].** A black iron sphere (radius 0.59 [D]) leaving the muzzle at **29.4, 30.5, 31.0, 33.3 u/s** (mean 31, `yadro` says 30 [D]), flying straight over 20 u with no visible arc. The tracked flight is 0.3-0.7 s long. A lob with `y` velocity 3 and gravity would reach the floor in 0.78 s and look flat from this camera [I]; its range is not measured.
- **On impact [M].** White-orange flash and the target is destroyed. At 6446.8 it hit a dynamite and the chain went off (F17).
- **Not consumed [M].** The left cannon fired twice (4.7 s apart), the middle twice (12.2 s apart). The number of hits it survives (energy 2 [D], 100 points) is not known [?].
- Level end: not required (second study, item 3).

**Spring** (`spring`, 21 in levels). **[?]** No contact that clearly hit it was seen. V1 level 24, 2634.0: the ball passed 2.6 u from the spring's centre and was reflected like off a wall (x velocity mirrored, speed 11-15 u/s afterwards, no kick) (F10). The spring looks like a blue crystal on a stone ring and stays through 11 s of other footage (F11, F12). Its material is an ordinary energy-1 brick of 10 points [D], so it may simply be a destructible bumper. Default for the game: a normal brick of 10 points, no extra kick, until a clean hit is found.

**Teleports** (`teleportsrc` to `teleportdest`). V1 level 67 `knight-fruktovie-derevia` (source (0.07,-4.46), destination (17.44,2.38)) at 8192.2 (F21) and 8213.7; V1 level 59 `cowboys-samba` (source (16.8,-6.1), destination (3.0,6.4)) at 7364.8 (F20).
- **What enters [M].** Only the ball was seen. Debris and prizes: [?].
- **Entry [M].** The ball vanishes when its centre reaches the source pyramid (last seen 1.1 u before the pyramid's centre at 8192.21).
- **Gap [M].** 0.13-0.25 s with no ball (level 59: 4 frames, level 67: 5-8 frames at 30 fps).
- **Exit [M].** The ball appears exactly at the destination position and grows from a speck (white pixels 4, 25, 49 over 3 frames). It then moves slowly for 0.15-0.3 s (about 0.7 u in the first 0.1 s) before leaving at 15-17 u/s.
- **Direction [M, 3 cases].** Always toward the bat side (-z) but not the same angle: about 0 degrees (level 59, v = (-0.9,-16.3)), -14 degrees (level 67 first), +45 degrees to +x (level 67 third). It does not follow the destination's orientation (level 59's is turned 31 degrees) and is not the incoming direction mirrored. Rule: **[?]**; build "exit at 15 u/s toward -z, angle randomised within about +-45 degrees".
- Two sources to one exit (`knight-zoo`, `village-dvorik2`): not watched [?].

**Dynamite** (`dynamite`, 116 in levels, big: radius 5 [D]). V1 level 67, ten dynamites along z = -2.8 at x = 18.2, 14.8, 11.1, 7.5, 3.5 | -3.5, -7.3, -11.0, -14.8, -18.2. The ball, just out of the teleport at x about 16.6, hit the right end at 8192.76 (F21, F23, F24).
- **Chain [M].** The five charges on the right went off within 0.1 s (flash first at the right end at 8192.83, over the whole right half at 8192.90): about 20-30 ms per link. The five on the left did not. The gap between x = 3.5 and x = -3.5 is 6.9 u, the other spacings 3.4-4.0 u. So the trigger radius (centre to centre) lies between 4.0 and 6.9 u, consistent with 5 [D].
- **Destroyed [M].** All the spiked Metall fence pieces within the right half (the left half of the fence is untouched), shown by shards flying (F23). A wooden log lying at the bottom right survived and was pushed.
- **Score [M, D].** Total **+1933** in two steps: +1187 at 8192.80 and +746 at 8192.90 (F22). Five dynamites are 600 [D]; the other 1333 come from fence pieces and others, not separable (Metall E1 is 50 each [D]) [?].
- A small dynamite (radius 3) and a ball-triggered blast outside chains: not watched [?].

**Firework** (`firework`, 63 in levels). V1 level 10 `piratos-bad-night`, a crate with a big red rocket and coloured shells on a raft at (-11.5,-8.4). F1, F2, F3.
- **Launch [M].** The ball brushed the crate at 1013.33. The rocket came off the crate at **1014.02**, about 0.7 s later. A second small rocket started to lift at 1014.23.
- **Flight [M].** Height 6.5 u at 1014.20 to 18.9 u at 1014.99: **15.6 u/s**, straight up. Burst at about 20 u at **1015.06**, so about 1.2 s of flight, not the 2.5 s of `lifeTime` [D, inferred]. The burst is a pink particle ball that spreads to about 25 u across by 1015.3 and fades by 1015.6.
- **Score [M].** None for the launch or the burst: the HUD stayed 76767 from 1013.0 to 1014.76 (F2). The later +17, +17, +180, +36 are other bricks.
- The crate was not destroyed by that touch [M]; whether a harder hit kills it (energy 1, 120 points [D]) [?; answered in 17.5]. The rocket and the burst did no damage that was visible [M].

**Volcano** (`volcano`, 13 in levels). **[M, weak]** Idle. V1 level 24, 2634-2704 (70 s): a static red cone with lava cracks, no eruption, no particles; a ball passed within about 1 u at 2647.4 with no reaction (F11, F12). Whether a hit or a timer makes it erupt: [?]. Build it as a static prop of 10 points.

**Generators** (`generatorgood`, `generatorbad`). **[M, weak]** Inert. In V2 193-203 two gift boxes stood unchanged for 10 s and the only prize in that time came from a destroyed brick elsewhere (F36). The second study watched V1 level 2 for 38 s (`rules-V1-L2-generator-watch-frames-t148-186`). They give 77 and 66 points [D] if destroyed. Build: static props that can be destroyed.

**Vibro** (barrels and rings, 23 in levels). **[M, weak]** V1 level 3, 215-243: the barrels do not travel, they tilt and bob in place (period about 2.4 s, position within +-0.1 u) (F9). They are kinematic and path-less [D]. Rings (`krug`) not watched [?].

**Flicker** (the cauldron `kotel` on a campfire, 13 levels). **[M]** Animated flame/spark/smoke particles; the light around changes by only 1.4 % (luminance 102.1-103.5 in an 80 px patch over 5 s) (F13). No gameplay effect seen.

**Metall** (452 in levels). **[M]** Dynamite kills a fence in 0.1 s (above). The second study saw five lightning bolts hit the Metall wall of level 31 with no score (`rules-V1-t3790.9-3793.1-L31-five-lightning-strikes-...`). So metal is hurt by blasts, not by bolts. How the ball bounces off it and whether the ball alone can destroy it: [?].

**Cloth** (sails and flags). **[D]** `wind` (0,0,+-10) and 3-8 particles per side; **[M, second study]** a sail scored +60, a small sail +20. Tearing: not seen [?].

**Trees** (128 in levels). **[D]** Trunk energy 5, 130 points, `LeafFall` particles; fruit held by breakable joints (force 10-20 on the apple tree). Not isolated in footage [?].

**Path movers** (35 sharks and birds, flag `0x01`). F4, F14, F19.
- **Speed [D, checked by M].** The binding's `param` is a float: speed in u/s = `param` x 1000. Values: **0.005** (5 u/s) on the sharks of `ostrovok`, `birthday`, `ribalka`, `zagoraem`, `zoloto-gorillas` and on both rings of `po-krugy`; **0.007** (7 u/s) on the `dva-doma` shark; **0.006** (6 u/s) on the ten birds of `cowboys-samba`. Measured: the level 1 fin moved 4.8 u/s (556 to 800 px in 1.76 s, F14); the `samba` birds 6.4-7.0 u/s over 2-3 s (F19, perspective bias +10 %).
- **Path [D].** Closed loop (path `param` 1), the actors go through the points in file order (clockwise for the rings). Loop lengths (Catmull-Rom, about): shark paths 33, 42, 58, 65, 66 u (periods 6.6-13 s); `samba` 48.5 u (8.1 s); `po-krugy` `krug1` 98 u with 10 ships/sharks (19.6 s a lap), `krug2` 71 u with 9 (14.2 s). Neighbours on a ring are 7.9-9.8 u apart.
- **They are bricks [M].** A level 1 shark died to one ball hit for 91 points (V1 54.13, section 5). They did not stop the ball in any frame; pushing the ball [?].
- Level end: kinematic bricks, disputed (section 9).

### 14.3 Prizes

**HUD, counts and timers [M].** A prize with a timer shows an icon (green ring good, red ring bad) and a number that falls 1 per second; a weapon shows the shots left. Icons: Big Ball 20 s from 87.5 (V2, F45); Power Ball 20 s from 121.0 and Night 10 s from 124.0 (V2, F30); Drunken 30 s from about 107.0 (V2, F25); Magnet 30 s from about 1299 (V1, F5). Big Ball's icon was cleared at the level change (V2 89.5), so timed effects end with the level. Speed, Surprise, Thunderbolt, Meteor, Wind, Explosive Ball and Multiball show no icon. Ammo rows stack under the timers.

**Weapons [M].** HUD counts F25, F32, F41, F46; flights F27, F28, F33, F34, F42, F47, F48.

| prize | shots | what leaves the bat | speed | what it does |
|---|---|---|---|---|
| Cannon | 6 | one iron ball from the bat centre, straight up the board | **42.6 u/s** (8 windows 35.7-50.5 over z -17 to +6, 96.65-97.20; `kernel` 40 [D]) | explodes on the first brick (flash white then orange, about 3 u wide, 0.35 s) and throws pieces; HUD 6 to 5 at 96.65, 5 to 4 at about 97.2 (two shots at least 0.55 s apart) |
| Bazooka | 7 | one rocket (red tip, smoke) from the bat centre | **54.5 u/s** (z -17.0 to -6.15 in 0.2 s; `rocket` 52.5 [D]) | explodes on impact, a white-orange star 4-5 u wide (0.1 s to 0.45 s); HUD 7 to 6 at 112.4; the treasure pile it hit stood afterwards |
| Machine Gun | 12 | **two bullets per shot**, 4.3 u apart (the barrels at +-2.15 [D]), small grey capsules | about 30-37 u/s (spacing of 20-24 px per frame at 60 fps; 30 [D]) | HUD counts shots, not bullets: 12 at 154.1, then 6 shots in 1.5 s (158.8-160.4) and 3 in 0.5 s (156.9-157.4), so 4 shots/s or faster is possible |
| Laser | 5 | a thin yellow beam with a red tip from the bat to the far end | the beam is full length within 0.1 s (about 150-200 u/s) and fades in about 0.45 s (62.78-63.26) | a red hit mark where it ends; HUD 5 to 4 at 62.87, 3 at 64.3, 2 at 64.8, 1 at 67.5; the E3 pirate it hit at 62.9 was not killed by one shot (planks are, second study) |

**Firing trigger [M, I].** One shot per press: the counts fall at irregular moments (cannon: two shots in 1 s, then 7 s of nothing; machine gun: intervals 0.2 s to 1.5 s). The click is not visible, so a mouse press is **[I]**. There is no sign of a minimum interval apart from the machine gun's of about 0.2 s. Weapon kills carry `disablePrize` for the meteor and the thunderbolt only [D].

**Magnet** (V1 level 13, F5, F6, F7). The bat becomes a red (left) and blue (right) horseshoe. A ball that touches the arch **sticks** to its top and rides with the bat: the ball stayed at the same offset (+0.75 u right of the bat centre, about 0.9 u above its middle) for at least 2.7 s (1309.9-1312.7) while the bat moved 18 u. Two balls stuck together (1310.18). A second ball stuck at offset -0.1 u (1317.1). It sticks where it touched [M]. A released ball flew off at about 1312.9 and was caught again at 1316.9. No pull from a distance was seen: the free balls flew straight until they touched [M weak]. The release trigger (a click as at the start of a level, or a timer) is not visible: **[?]**; build "release on click". Total 30 s.

**Yo-Yo** (`pingpong`). **[I, M]** At 125.99 (V2, F31) a green-to-blue string appears between the bat and the ball (the `rubber` particle [D]) and stays until 133.5 (7.5 s). The caption was hidden under "Night" (the letters overlap at 126.0), and the HUD shows no Yo-Yo icon, so the identification is from the string. During it the ball is **kept within 7.0 u of the bat** (string length 1.1 u at the start, 2.3 u while it trailed the bat, 4-5 u on its arcs, 7.0 u at the end), moves slower and unevenly (6.6-13 u/s) and swings in arcs that pull back to the bat. Build: ball tethered to the bat by an elastic, maximum 7 u, for about 7.5 s [I]; the duration is a single case [?].

**Drunken Ball** (30 s, V2 108-137; F25, F26). **[M]** The ball's heading wobbles: in smooth stretches of 0.3-0.7 s it turns at **15-22 degrees per second** (+16.6, +21.9, +17.0, -6.6, -6.7, -31.8), with the sign changing about once a second. The median change in heading per 0.05 s is 6.3 degrees (75th percentile 11.6) against 3.5 (7.5) in the same video without the prize (V2 139-153). Speed unchanged. Build: rotate each ball's velocity at 20 degrees per second x sin(2 pi t / 1.6 s + phase) [I].

**Big Ball and Weak Ball** (20 s each; F38). **[M]** Ball size from the area of the white panels divided by the pixels-per-unit squared: Big Ball area x2.6, so diameter **x1.62** (r about 0.97; the `ball-powerfull.x` mesh is 0.94 [D]); Weak Ball area x0.40, diameter **x0.63** (r about 0.38). Build r = 1.0 and 0.4 [I]. Normal Ball ends a size effect (the Weak icon vanished at 376.0-376.7, section 7).

**Power Ball** (20 s). **[M]** The ball is drawn as a dark spiked ball (V2 125.9+, V1 level 24 2647.3). Whether it passes through bricks: **[?]**; the footage of its 20 s has the yo-yo string on it and the bricks hit were small (F29, F31).

**Explosive Ball.** **[M, weak]** No icon; the ball looks unchanged. At the first brick it touched after the catch (V2 174.55-175.31, F35) an orange star with a pair of blue arrows spread to about 5 u across in 0.75 s and the barrel it hit rolled away. Trigger radius 2.5 [D]; the real blast radius, the number of blasts per catch: [?].

**Increase / Decrease Ball Speed.** **[M]** Increase: 15.6 to 17.2 u/s (V2, section 2). Decrease (V2, caption at 986.4): median ball speed 18.67 before and 16.82 after on the same scale, **ratio 0.90-0.91** (0.915 with the other time base) (F49). So Decrease is x0.91, the inverse of Increase x1.10. No timer. Whether the factor lasts across levels, and any limit on stacking: [?].

**Multiball** (`tripple`). **[M]** One catch makes **three balls** (V2 333.7 and a second catch at 713.7, F39, F43). Two new balls appear within 1.6 u of an existing one; they fly apart at once: in the first case headings of about -20 degrees (up and left) and +98 degrees (nearly sideways along the bat line) next to the original, speeds 15-19 u/s. Build: add two balls at the ball's position with +-50 degrees around its heading [I]. A life is lost only when the last ball goes (section 2).

**Night** (10 s). **[M]** The board's mean luminance falls from 52-58 to 21 (x0.38) and the sky from 72 to 35 (x0.49) within one second (124.0-125.0), holds for 9 s, and is back to about 80 % at 134.0 (F29, F30). Everything stays visible, dimmer: bricks, the white ball, prize halos, the HUD.

**Thunderbolt.** **[M, weak]** A catch gives a **burst of 2-3 bolts within 0.7 s** at different places (V2 407.97-408.70; white-blue jagged lines from the top edge, each visible about 0.1 s, F40), and more strikes later (415.9, 419.8). V1 level 2 gave bolts at 200 and 206.5 after one caption (second study). Targets are bricks of the pile; how many strikes, over how long, and what chooses the target: [?]. The first study's doubts about who throws the bolts stand.

**Meteorite Rain.** **[M]** About five meteors over five seconds after the catch (V2 859.5-865): a red-orange streak falling diagonally, then a white expanding ring on the water about 0.4 s long (rings at 860.5, 861.3, 863.1, 864.3) (F44). `meteor` velocity (-10,-30,10) [D].

**Earthquake.** Footage of V2 179.8-184 (section 7, `nocommentary-DpvwN-k35Eg-t179.8-184-earthquake-no-camera-shake.jpg`) stands: about 3 s of jitter and jumps, no camera shake. Strength and which bodies move: [?].

**Wind.** **[M, weak]** Caught at about 383 (V2). No icon, no visible particles (the `Tornado` sprite `ParticleRoket.png` was never shipped [D]) and the ball's path stayed straight: heading rate 0.1 degrees/s over 1.5 s (392.3-393.8). Effect on loose pieces: [?]. Build: caption and sound only.

**Surprise** (`random`). **[M]** The orb is a gift box with a "?" under three balloons (red, blue, green) with an orange halo (V2 193.0-195.3, V1 2646-2647; F11, F36). No HUD icon and no change in the bat, the ball or the board in the 9 s after the catch (V2 195.3-204, F37). What it does: **[?]**, so build it as an instant random good prize.

### 14.4 Prize drop details

- **Fall [M].** Straight along -z, x constant (+0.2 u over 3 s, V1 134-135 with the bat 26 u away; F8), speed 9.7 u/s (V1 132.2-135.1), 9.9 and 11.0 (Weak Ball, V1 2680.6-2682.2), 8.8 (Surprise, V1 2646.2-2647.1): mean about **10 u/s**. The first frames after the brick dies are slow (7 u/s, about 0.3 s), then steady. It is **not** homing on the bat. Source: where the brick was (V1 132.2: x 12.8, z 3.0).
- **Catch [M, I].** Caught at |dx| = 1.8 u (V2 195.2); missed at 8.8 u (V2 193.8) and 26 u (V1 134). Rule [I]: |dx| less than about 4 u at the bat line.
- **Missed [M].** The orb keeps going to z = -21.1 (V1 135.12) and fizzles there in a blue sparkle ring of about 0.2 s (F8). It is gone, no score.
- **Several at once [M].** Yes: V2 193.0-193.8 two orbs (a green one and the Surprise box) fell along the same line 2 s apart (F36); V1 134-135 shows one prize falling while others are being caught.
- **Looks [M].** Surprise: "?" box with balloons. Weak Ball: a grey "100" weight in a red halo (V1 2680.6-2682.2, F12).

### 14.5 Corrections to earlier sections

- Section 7 "moves straight toward the bat": it falls straight down the board at about 10 u/s; it does not steer.
- Section 7 "missed prizes vanish [I]": they fizzle in sparkle at z = -21 [M].
- `OBJECTS.md` fire rocket `lifeTime` 2500: the rocket burst 1.2 s after launch (rise 15.6 u/s to about 20 u).
- Table of section 7: Magnet, Yo-Yo, Drunken Ball, Night, Wind and Surprise rows now have the facts above. The Wall row stands.
- Spring, volcano, generators: "[inferred] periodic or kicking" is not supported by the footage (spring [?], the others idle).

### 14.6 Evidence frames (`reference/gameplay/special-*` next to the repository, not in it, 49 files, 7.6 MB)

F1 `V1-t1013.20-1014.23-L10-firework-crate-ball-passes-rocket-leaves-0.7s-later` | F2 `V1-t1014.0-1018.5-L10-points-76767-76784-76801-76981-77017-no-score-for-firework` | F3 `V1-t1014.20-1015.52-L10-firework-rises-15-units-per-second-and-bursts-pink` | F4 `V1-t1123-1141-L11-two-rings-of-sharks-circle-island-ball-destroys-sharks-sheet-2s-steps` | F5 `V1-t1296-1334-L13-magnet-30s-timer-then-cannon-prize-6-shots-counted-down` | F6 `V1-t1309.40-1312.30-L13-magnet-bat-catches-and-holds-balls` | F7 `V1-t1312.3-1330.3-L13-magnet-release-recatch-and-end` | F8 `V1-t134-135.2-missed-prize-falls-straight-down-and-fizzles-at-near-edge` | F9 `V1-t215-223-L3-vibro-barrels-bob-and-tilt-in-place-0.5s-steps` | F10 `V1-t2633.9-2634.5-L24-ball-passes-2.6u-from-spring-plain-reflection-no-kick` | F11 `V1-t2646.20-2647.50-L24-surprise-prize-question-box-with-balloons-falls-straight-volcano-and-spring-idle` | F12 `V1-t2679.8-2690.2-L24-weak-ball-prize-falls-straight-9-11-u-per-s-volcano-idle` | F13 `V1-t3011-3015.8-L26-flicker-cauldron-campfire-flame-and-smoke-no-gameplay` | F14 `V1-t52.30-54.06-L1-shark-fin-moves-right-5-units-per-second` | F15 `V1-t6440.10-6440.77-L51-cannon-hit-by-ball-fires-black-cannonball` | F16 `V1-t6440.60-6441.44-L51-cannonball-flight-up-the-board-and-impact` | F17 `V1-t6446.20-6447.45-L51-cannonball-hits-dynamite-chain-then-second-cannon-fires` | F18 `V1-t6446.85-6447.29-L51-debris-hits-cannon-and-it-fires-with-ring` | F19 `V1-t7342.0-7343.02-L59-blue-birds-move-on-samba-loop-fence-static` | F20 `V1-t7364.55-7365.36-L59-teleport-ball-enters-and-exits-straight-down` | F21 `V1-t8191.95-8192.89-L67-teleport-ball-enters-portal-in-exits-portal-out-next-frame` | F22 `V1-t8192.6-8194.6-L67-points-441889-443076-443822-dynamite-chain` | F23 `V1-t8192.70-8193.91-L67-dynamite-chain-right-half-only-gap-stops-it` | F24 `V1-t8192.76-8192.97-L67-dynamite-blast-flash-per-charge` | F25 `V2-t108-117-hud-cannon-1-left-drunken-30s-bazooka-7-6-5-3` | F26 `V2-t108-153-drunken-vs-normal-ball-tracks-plot` | F27 `V2-t112.30-113.30-bazooka-rocket-fires-from-bat-hits-treasure` | F28 `V2-t112.58-112.91-bazooka-rocket-closeup-impact-flash` | F29 `V2-t119-134-night-prize-darkens-board-10s-and-power-ball-green-trail` | F30 `V2-t121-136-hud-power-ball-20s-and-night-10s-timers` | F31 `V2-t125.0-131.0-yoyo-ball-on-string-green-blue-line-to-bat` | F32 `V2-t153.6-160.6-hud-machine-gun-12-shots-4-per-second-burst` | F33 `V2-t159.90-160.48-machine-gun-burst-twin-bullets-from-bat` | F34 `V2-t160.12-160.27-machine-gun-twin-bullets-closeup-rising` | F35 `V2-t174.55-175.31-explosive-ball-blast-flash-and-arrows-at-barrel` | F36 `V2-t193-203-surprise-balloon-box-two-prizes-fall-at-once-then-explosion-prize` | F37 `V2-t194.5-204-hud-no-icon-after-surprise` | F38 `V2-t259-376-ball-size-crops-normal-big-weak` | F39 `V2-t333.50-334.00-multiball-one-ball-becomes-three` | F40 `V2-t408.00-408.70-thunderbolt-bolts-strike-wood-blocks-in-0.7s` | F41 `V2-t61.6-69.6-hud-laser-5-4-3-2-1-shots` | F42 `V2-t62.78-63.42-laser-beam-fires-up-fades-in-0.5s` | F43 `V2-t713.40-714.48-multiball-second-catch-three-balls-fan-out` | F44 `V2-t858-865-meteor-rain-streaks-and-impact-rings` | F45 `V2-t86.0-92.5-bigball-hud-timer-cleared-at-level-change` | F46 `V2-t95-104-cannon-prize-hud-count-6-5-4` | F47 `V2-t96.55-97.39-cannonball-flight-from-bat` | F48 `V2-t97.15-97.60-cannonball-impact-explosion` | F49 `V2-t976-997-slowdown-ball-speed-before-after-plot`.

F26 and F49 are plots drawn from the tracked positions (not screenshots).

### 14.7 Still open (answered for the game in 17.8; the weapons' and strikes' items in 18.9)

- Spring: does it kick the ball or just bounce it, and does one hit destroy it (energy 1, 10 points)? Only a graze at 2.6 u (V1 2634.0, level 24) was seen. A clean hit in one of the 21 levels (22, 25, 28, 33, 38, 46 and the cowboys levels have springs near walls or the near half) would settle it.
- Interactive cannon: how many hits destroy it (energy 2), what damage the cannonball does and how far it flies (a lob with y velocity 3 under gravity would land at about 23 u), whether a very slow ball or a bat shot fires it, and whether a cannon ever fires on a timer (none in 28 s).
- Teleports: the rule for the exit angle (0, -14 and +45 degrees in three cases, always toward -z), whether prizes, debris or bullets use portals, and which exit two sources share (knight-zoo, village-dvorik2).
- Dynamite: the blast reach is only bracketed (trigger between 4.0 and 6.9 u centre to centre), small dynamite (radius 3) was not seen, and the +1333 beyond the five 120-point charges in the level 67 chain is not decomposed.
- Firework: is the launch triggered by the ball touching the crate (one touch at 1013.33 gave a launch 0.7 s later), a periodic timer, or a hit after a delay? How many rockets per crate, and does a harder hit destroy it for its 120 points?
- Volcano and generators: no eruption in 70 s and no prize from generators in 10-38 s of footage; if they do act, what triggers them? Unknown whether the ball can destroy them.
- Metall: how the ball bounces off it and whether the ball alone ever destroys it; cloth sails: do they tear; trees: do the fruit fall when the trunk or a fruit is hit.
- Power Ball: does it pass through bricks? Explosive Ball: the blast radius, the number of blasts per catch, and what it damages. Yo-Yo: confirm the caption and the duration (7.5 s in one case) and the 7 u string limit. Magnet: is the release a click or a timer (2.7+ s stuck in one case)?
- Multiball: the exact angles of the two new balls (one case measured: -20 and +98 degrees about the original) and whether the extra balls spawn at the ball nearest the bat or at the bat.
- Surprise: which prize it gives, since no icon or effect was visible in 9 s. Wind: any effect on loose pieces. Thunderbolt: strikes per catch, the span in time (bolts up to 12 s after a catch) and what picks the target. Meteor Rain: count (about 5), targets and damage. Earthquake: force and which bodies move.
- Weapon damage: hits needed against energy 2-4 bricks for the cannon ball, the rocket, the bullets and the laser (the E3 pirate survived one laser beam at V2 62.9), and the blast radii of the cannon ball and the rocket. Minimum interval between shots (machine gun about 0.2 s; the others none seen).
- Ball speed prizes: do the +-10 percent steps persist across levels, is there a floor or ceiling, and does Normal Ball reset them? Do timed prizes always end at the level change (only Big Ball's icon at V2 89.5 was seen)?
- Path movers: do they push the ball, and are the sharks and birds required to end a level (second-study item 2 is disputed)? Two rings of 10 and 9 actors on `po-krugy`: the start phase of each actor is the position of its key in the level data, not checked against footage.
- Prize catch: the real overlap rule (the 4 u rule here rests on a catch at 1.8 u and misses at 8.8 and 26 u), and whether a second prize of the same kind caught during the first one's timer adds time or replaces it.

## 15. Prizes as built (step 5, prizes)

What the game does with the prizes of sections 7 and 14 where they leave room; `[I]` is a choice made from the footage rather than a measurement. The rules (what drops, how it falls and is caught, what each of the 28 does, and the state and events the picture reads) are `game/Prizes.h` (`PrizeRules`, the table `prizeInfo`) and `GameSession` (`game/src/SessionPrizes.cpp`, called from `Gameplay.cpp` at a few marked places); the sounds, the HUD and the look are the presentation's. Tests: `mb_prize_tests` (made-up levels), `mb_prize_smoke` (the table against the real data), `mb_gameplay_smoke` (the autopilot with the prizes on).

### 15.1 What drops

| rule | in the game |
|---|---|
| when | each destroyed object (`BrickDestroyed`, any damageable object: bricks, trees, metal, dynamite...) is rolled once, with a random generator seeded per session (`SessionOptions::prizeSeed`, SplitMix64, the same numbers on every machine); a piece that fell off the board is no kill and leaves nothing; nothing drops once the level is complete; a hit that carries `disablePrize` (`GameSession::hit(actor, at, false)`, for the Thunderbolt and the meteors [D]) leaves nothing |
| how often | by what the object was worth (`PrizeRules::tiers`): 15.8 % below 80 points, 10 % to 119, 6.5 % to 199, 5 % to 299, 7 % from 300. The second study's fit by value is 13.5, 8.5, 5.5, 4.2 and 5.9 % of the kills that had a caught prize; the numbers here are those raised by 1.18, so that the mean over the 13,231 destroyable objects of the 83 levels (11,282, 889, 789, 210 and 61 in the five tiers) is **14.64 %**, the 14.7 % of all kill events [M, I]. `mb_prize_smoke` prints the mean from the data |
| bad prizes | the seven with a red halo (Skull, Decrease Bat Size, Increase Ball Speed, Weak Ball, Normal Ball, Drunken Ball, Night) drop from campaign level `PrizeRules::badFromLevel` = 10 on, none before [M]; `SessionOptions::campaignLevel` carries the level's place (the HUD's number) |
| which | drawn by weight from the table below, over the prizes allowed. There is no weight in the data [D]; the weights are the second study's counts of caught prizes by score jump (levels 2 to 83, 1,320 of its 1,355), a value shared by two prizes split by what the footage shows of each [I]. They give the bad ones 11.5 % of the drops (9 % of the catches of levels 10 to 83 in the footage had a red-only value, a few more with the three that share one) |
| the Next Level star | **rolled apart from the others**, once for each destroyed object, and left out of the weights: it ended 70 of 83 levels, always with most of the board gone, and was 5 % of the catches [M], so it cannot be a drop of constant chance (that would end levels early, and a fifth of them never). The chance is 0 until fewer than a tenth of the level's targets (at least 6) are left, then rises in a straight line to 0.5 for the last one (`starChance`). A star in the air blocks another, and a level that never ends (`TargetRule::Never`) has none. It falls in nine levels of ten (97 % of the levels of 100 bricks destroyed one by one, never before the last ten; in the 83 levels the autopilot plays for 15 minutes, 66, and the 12 it does not finish are most of the rest: 15.5), and a player who catches nine stars in ten would end four levels in five, the footage's 70 of 83 [I] |

The weights (`PrizeInfo::weight`), their halo and what the footage had: Bonus 146 and Life 7 (777 together: 153 jumps; the hearts rose from 2 to 6 in three hours, two lost), Increase Bat Size 135, Decrease Bat Size 26 and Big Ball 40 (180 together: 66), Increase Ball Speed 83 and Night 5 (the 88 jumps of 280, 5 darkened the scene), Decrease Ball Speed 22 and Normal Ball 17 (the 39 jumps of 120 that are not stars), Wind 87, Explosive Ball 81, Thunderbolt 81, Power Ball 77, Multiball 40 and Earthquake 25 (190 together: 65), Machine Gun 64, Magnet 58, Cannon 54, Surprise 51, Bazooka 45, Wall 39, Laser 39, Yo-Yo 11, Drunken Ball 8, Weak Ball 6, Skull 2 and Meteorite Rain 30 (worth 4 points, which no score jump shows: a guess). The values of 110, 120 and 150 are also the price of bricks, so Increase Bat Size, Explosive Ball and Normal Ball may be too high [?].

### 15.2 Fall and catch

| rule | in the game |
|---|---|
| fall | from where the object was (its centre of mass at `BrickDestroyed`: x and z), at y 1.0, straight along -z at **10 u/s**, x unchanged and not homing; several at once. The slower first 0.3 s of the footage (7 u/s, 0.45 u) is left out [I] |
| catch | each step, if the orb is within **batRadius + 1** of the bat's centre in the plane (4 u for the plain bat; a wider bat reaches farther). The bat must be there: not as it swoops in, once the last ball is lost, or while it flies off. A prize already falling when the level is won is still caught, and pays its score and, for a Life, its heart; nothing else it does |
| score | the catch adds the prize material's `brickScore` (`PrizeCaught::points`, the material in `PrizeCaught::material`, whose dead sound is the catch's sound). `meteor` names two materials in `materials.json`, the projectile's first (250) and the prize's (4): the prize is the one in the group `Prizes` |
| missed | at z = -21 (the bat's line) it fizzles (`PrizeMissed`), no score |
| caption | `prizeInfo(kind).caption`, the key in `languages.json` (`Drunken Ball` has a space, Weak Ball is `Weak`) |

### 15.3 What they do

A **timed** effect starts over from its full time when the prize is caught again (it does not add up, and stays one effect with one icon) and ends with its time or with the bat; **weapons** and **Explosive Ball** count shots and blasts, and a second catch adds them. The footage never watched a second catch [?]. Timers count the session's fixed steps; the HUD shows `ceil(timeLeft)`.

| prize | in the game |
|---|---|
| Bonus | the score only |
| Life | +1 heart, no cap |
| Skull | the bat dies at the catch: `phase()` is `Lost` at once, the bat flies off 0.4 s later (`BatLeft`), every ball (held ones too) vanishes 1.5 s after the catch (`BallLost`), the heart goes at 1.9 s (`LifeLost`, `BatEntered`) and the ball is ready at 2.43 s: the life sequence of a lost ball, run as if the loss began `skullLead` = 0.2 s before the catch [M, V1 8536.6]. With no heart left it is `GameOver` |
| Next Level | `forceLevelComplete()` |
| Increase / Decrease Ball Speed | the ball speed of the session (`ballSpeed()`) times 1.10 or 0.91 for each catch, no timer and no icon; the balls in flight change at once. Limits 0.5 to 2 times [?]. It stays through the loss of a bat and is gone with the level [I: the footage has 15.0 u/s at levels 1 and 65] |
| Increase / Decrease Bat Size | the bat's collision sphere and its look (`batScale()`, `batRadius()`) times 1.24 or 1/1.24 for each catch, between 0.6 and 1.9 times; the bat is rebuilt at the new size, its outer edge stays inside x = 19 (the walls are at 20: the centre is held to 19 minus the radius when that is under 16) and the held ball sits in front of it. A new bat after a loss is plain |
| Big Ball / Weak Ball | radius 1.0 / 0.4 (`ballRadius()`, `BallView::radius`) for 20 s; the balls in flight are rebuilt (the same velocity). One replaces the other |
| Power Ball | 20 s. The footage does not say if it passes through bricks [?]; here every hit of the ball takes **all** the energy of the brick (a bullet, a blast or a thunderbolt still do one). The alternative, a ball that goes through, needs a ball the bricks do not stop |
| Explosive Ball | one blast: the next brick the ball hurts is hurt as usual and everything damageable within 2.5 u of the contact point (the distance to the box round it) takes a hit as well, from the radius of `explose.xml` [D] (`Blast`, `speed` holds the radius); then it is spent. No icon, no timer |
| Normal Ball | ends Big Ball, Weak Ball, Power Ball, Explosive Ball, Drunken Ball and Yo-Yo. The speed prizes and the bat's are not ball effects |
| Drunken Ball | 30 s. Each ball's heading is turned by 20 degrees/s x sin(2 pi t / 1.6 s + a phase of its own) every step, so it wobbles about 10 degrees from side to side with the speed unchanged [M, I] |
| Multiball | on the ball nearest the bat (not a held or stuck one; with none, nothing happens), two balls beside it, 1.3 u away on headings turned by -20 and +98 degrees from its own [M, one case]; at most 12 balls. A life is still lost only with the last |
| Magnet | 30 s. A ball that touches the bat sticks where it touched (`BallStuck`, `BallView::stuck`, still, on the bat at the same offset) and goes with it; a click (`GameInput::launch` with no ball held), 4 s without one, or the end of the magnet lets it go along the line from the bat's centre through the ball, and it does not stick again until it has left the bat [M, the release I]. The autopilot clicks |
| Yo-Yo | 7.5 s. A ball farther than 7 u from the bat's centre and going further is turned back (mirrored about the string's direction) [M, I]. No icon (the footage shows none) |
| Wall | 30 s. A ball on its way to the near edge turns back at z = -19.04 (the far face of the 0.74 deep rope box at z -20.01 [D], plus the ball's radius), as off a wall (`BoardHit`), anywhere across the board's width |
| Night | 10 s. `nightLevel()` runs 0 to 1 over the first second, holds, and goes back to 0 over the last 1.5 s (the board's brightness falls to 0.38 of itself within a second and is back at about 80 % at the end [M]) |
| Earthquake | 3 s. Every 0.1 s each dynamic body, asleep or not, gets a kick of up to 0.5 u/s sideways (x and z) and 1 u/s up [I]; nothing for the camera |
| Wind | 5 s. Every dynamic body that is awake is accelerated 2.5 u/s^2 along x, to the left or the right (drawn when it is caught); resting pieces and the balls are left alone [I: the footage shows nothing] |
| Surprise | an instant gift: a random **good** prize that is no Surprise and no Next Level, by the same weights, announced as a second `PrizeCaught` worth 0 [?] |
| Machine Gun, Cannon, Bazooka, Laser | 12, 6, 7 and 5 shots (`weapon()`, `shotsLeft()`, `useShot()`). The weapon caught last is the one a click fires (section 18); the others keep their shots, as the footage's HUD shows two at once (V2 108-117); the effect ends with its last shot |
| Thunderbolt, Meteorite Rain | `StrikeGranted` with the prize, and the strikes begin (18.5, 18.6) |

When the **bat is lost** (the last ball, or the Skull) every effect of the bat and the ball ends (`EffectEnded`) and the next bat is plain; the night, the quake and the wind run on, and so does the speed. Timed effects end with the level, which is a new session.

### 15.4 What the presentation reads

- `fallingPrizes()` (`FallingPrize`: kind, position of the orb, age, source), `activeEffects()` (`ActiveEffect`: kind, `timeLeft`, `duration`, `shots`; those with `prizeInfo(kind).icon` go on the HUD, oldest first, as the number `ceil(timeLeft)` or `shots`), `weapon()`, `ballRadius()`, `ballSpeed()`, `batRadius()`, `batScale()`, `nightLevel()`, `BallView::stuck` and `radius`.
- Events: `PrizeDropped`, `PrizeCaught` (prize, points, material), `PrizeMissed`, `EffectStarted`, `EffectEnded`, `StrikeGranted`, `BallStuck`, `Blast` (the weapons' and strikes' own are 18.8); `SessionStats::prizesDropped/Caught/Missed`.
- `GameSession::dropPrize(kind, from)` starts a prize falling and `givePrize(kind)` applies one, for tests and tools.

### 15.5 The autopilot with the prizes

`mb_gameplay_smoke --seconds 900`, each level played as the n-th of the campaign, the prizes on, the autopilot as before (the bat under the ball, no aim, 99 hearts; it clicks to let go of the Magnet and catches prizes only when they fall on the bat by chance):

**71 of 83** levels finished (60 without the prizes): 54 by destroying every target and 17 only because the stall rule let go of the last few (it was 46 and 14), 12 not finished (6 knights', 5 village's and 1 pirates' boards, 1 to 116 targets left; 11 were still destroying bricks in the last 40 s, the 12th, knight-nazad-k-mame, with one target left, stood still for 37 s: slow, not stuck, as before). 2,063 prizes dropped (25 a level, as 14.7 % of about 170 destroyed objects), 709 caught (a third: the bat is under the ball, not under the prize) and 1,339 missed. The Next Level star fell in 66 of the 83 levels, was caught in 25 and ended 25 of the 71 finished (a player who goes for it ends most, section 14.4); the other 46 ended as before. The look at what the ball can reach puts 194 targets out of play (it was 151): a quake or a wind that throws a piece out of reach takes it out of the count, which is the rule working as meant. Three balls were freed from a pin in as many levels. Nothing broke: the ball keeps its speed (the session's, which the speed prizes move) and its plane after every frame, in every level.
These numbers are without the special objects of section 17; with them the run finishes 68 of 83, and with the weapons of section 18 as well 75 of 83 (section 12).

## 16. Prizes on screen and in sound (step 5, stage 2)

How the picture and the sound show section 15. The code is `game/PrizeScene.h` (the orbs, the halos, the flashes, the models of the balls, the Magnet's horseshoe, the Wall's rope, the Yo-Yo's string, the night's light), `game/PrizeHud.h` (the icons and their numbers, the caption) and `game/PlaySounds.cpp`, called from `PlayScene`, `PlaySession` and `drawHud` at a few places. `[M]` is measured from the footage (`reference/gameplay`), `[D]` read from the data, `[I]` a choice made where both are silent; every constant is named in `PrizeScene.cpp`, `Hud.cpp` and `PrizeHud.h` with its tag.

### 16.1 The orbs

| what | in the game |
|---|---|
| the model | the prize's mesh, `Data/Meshes/prizes/<stem>.glb` (`Prizes/<stem>.x` in the file `Data/Objects/Prizes/<stem>.json`), drawn with its own textures or its material's (`bonus` has none: its mesh's `Dollar.jpg`); the 28 meshes have their own sizes (a coin of 1.85, the Surprise's box and balloons 5.9 tall) and their origins in different places (the floor under a coin, the middle of the Magnet), so the middle of the model's box goes to the orb's centre (y 1.0) and no model goes through the floor [I]. It turns about the vertical at 2.5 rad/s, each prize a little out of step with the next [I] |
| the halo | the two sprites of `Particles/Oreolblue.xml` (good) or `Oreolred.xml` (bad) [D]: the Oreol, a star that faces the camera and turns at 3.5 rad/s (`Oreols/02.png`, orange; red: `11.png`), drawn additively and behind the model, and the OreolShadow, a mark lying on the floor 0.1 up that turns the other way at 1.5 rad/s (`07.png` blue, red: `09.png`); both about 4 u across, about twice the orb as the frames of V1 134.4 look [I] |
| caught | the halo swells and fades over 0.3 s where the orb was [I] |
| missed | at z -21: a ring of light that spreads and 8 blue sparks that fly out, 0.25 s (the footage: a small blue sparkle ring of about 0.2 s) [M, F8] |
| Explosive Ball | `Blast`: the orange star with the blue arrows (`Oreols/02.png` and `03.png`), the blast's diameter (2 x 2.5) in 0.75 s [M, F35] |

### 16.2 The caption and the icons

The caption is the prize's `languages.json` string (`PrizeInfo::caption`), green, in the bookworm font at 28 on the line of the points, centred, with a dark shadow: **strong for 1 s and gone at 2.2 s, fading in a straight line between** [M, section 6]. A catch replaces the caption on show and starts it over; the gift of a Surprise (a catch worth no points) does not: the footage shows "Surprise" and nothing else [M]. It is cleared with the powers when a level is won.

The icons (`Hud.cpp`, `PrizeHud.h`): one for each effect in force that has one (`PrizeInfo::icon`: Big Ball, Weak Ball, Power Ball, Drunken Ball, Magnet, Wall, Night and the four weapons), oldest first down the right edge, with the number to its right in the same green: `ceil(timeLeft)` for the timed ones (so it reads 20, 19, ... and is 1 for the last second) and the shots left for a weapon [M, V2 61.6-69.6, 87-124]. The ring is the icon's own, green for a good prize and red for a bad one (`mb_prize_smoke` checks that each icon's ring has the colour of its prize's halo); nothing draws a ring. The number is drawn at font size 24, left-aligned 4 reference pixels right of the 48-pixel icon, and 34 pixels of room are kept for it at the edge (1024x768 units), which moves the icons left of where the placeholder HUD had them [I]. Instant prizes, the Explosive Ball, the Earthquake, the Wind and the Yo-Yo have no icon [M].

### 16.3 What the effects look like

| effect | picture |
|---|---|
| bat size | the bat's model is drawn `batScale()` times its size, all of it (the collision sphere is round); it was 24 % wider per catch in the footage (section 7) |
| ball size | the ball and its shadow are drawn `radius / 0.6` times their size, and roll by the distance over their radius |
| Power Ball, Explosive Ball | `ball-powerfull.x` (a spiked ball) and `ball-explosive.x` (a bomb with a fuse), each as wide as the plain ball's model (the first is 1.9 across, the second 1.3, the plain ball 1.3) [D, I]. The footage has the Power Ball dark and spiked (V2 125.9) and the Explosive Ball looking unchanged [M, weak]; the data has the model, so it is drawn |
| Magnet | the `Magnit` frame of `bat.x`, a horseshoe red on the left and blue on the right, on the bat as long as the effect lasts (V1 1309.4); a ball it holds goes with the bat |
| Wall | the rope, `wall.x` (the box of `Bats/wall.XML`: 41.4 x 3.2 x 0.7 at z -20), with its `Bonus-Stena` texture, rises out of the floor in 0.3 s and sinks in the last 0.3 s [I] |
| Yo-Yo | a string from the top of the bat to each ball in flight, green at the bat to blue at the ball (the `rubber` particle's `colour1` and `colour2` [D]), 0.22 u wide [M, V2 125-131] |
| Multiball, Drunken Ball | the extra balls and the wobbling path are the session's |
| Night | the scene's light is dimmed to 0.38 of itself at full night (`nightLevel()`), and the sky, the halos and the flashes, which have no light, to 0.49 [M, section 14.3]; the HUD, drawn over it, is not dimmed |
| Earthquake, Wind | the session throws the loose pieces about, and that is all there is to see; the camera does not shake [M] |
| the weapons | **not drawn yet**: their firing is section 18 and their picture the next stage's. The barrels (`Pulemet-*`, `Pushka`, `Bazuka-*`, `lazer-*`) are frames of `bat.x` that the game moves: in its `Common` animation each grows from a tenth of its size to full size in 0.6 s, recoils and shrinks again, and the glTF files keep that animation in `extras.animations` (`ASSET_FORMATS.md`) |

### 16.4 Sounds

| event | sound |
|---|---|
| `PrizeCaught` | the prize material's `dead` sound, panned by where it was caught [D]: `bigball`, `night`, `life`... (`money`, the Bonus's, and `bazuka` name sounds with no file in `sounds.xml`: those catches are silent, as in the original). The Next Level star's `endlevel` is the level's own, which `LevelComplete` plays in the same step. A Surprise plays its own and its gift's |
| `BallStuck` | `ballMagnited` |
| `BallRubbered` (new: the Yo-Yo's string turned the ball back) | `ballRubbered` |
| `Blast` | `explosion_ball` |
| `PrizeDropped`, `PrizeMissed`, `EffectStarted`, `EffectEnded`, `StrikeGranted` | none: the data names no sound for them |

### 16.5 Looking at it without waiting for the drops

`MuggleBall3 --autoplay [--level <name>] [--drop-prize stem[@seconds[,x]]] [--catch-prize stem[@seconds]]` (repeatable; with `--screenshot out.png --simulate <seconds>` the level is played that long and drawn): `--drop-prize` lets the prize fall from the far end of the board (z 18) at the bat's x, or at `x`, after `seconds` of play; `--catch-prize` puts it on the bat when the bat is there, and it is caught at once (score, caption, sound, effect). `stem` is the file name in `Data/Objects/Prizes` (`bonus life scull incbat decbat speedup slowdown bigball smallball powerfull commonball explosiveball drunken tripple magnit pingpong wall night gun cannon bazuka lazer flash meteor quake wind random endlevel`, any case). The autopilot repeats (its prize seed is the level's number), so the same command draws the same picture. `--hud-state lives,score,level[,icon[:number]...]` takes numbers too (`Cannon:6`).

### 16.6 Still open

- The weapons' barrels (above).
- The halo's real size, the orbs' spin and the models' height above the floor are not measured.
- The icons' own place in the original's HUD (the numbers sit right of the icon at the right edge in the footage; the icon's exact x is a guess).
- Whether the Explosive Ball is the bomb in the original's footage (it looks like the plain ball in the one case seen).
## 17. Special objects as built (step 5)

What the game does with section 14. The rules are `SpecialObjects` (`game/SpecialObjects.h`): `GameSession` owns one and calls it at the three places the level's special objects touch the rules of play (a ball with a blow met one, a charge of dynamite was destroyed, once a fixed step), and its numbers are `SpecialObjectRules`, `GameplayRules::objects`. The picture is `ObjectEffects` (`PlayScene` draws it) and the sounds are `PlaySounds`. Tags as before; `[I]` is a choice made where the footage and the data leave room, and each question left open in 14.7 is answered in 17.8. The numbers are in 17.9.

### 17.1 What hurts what

The ball hurts bricks, trees, dynamite, firework crates, vibros and flickers, one energy a blow, as before (section 5). The other special objects (cannons, springs, volcanoes, generators and metal) react to it and are hurt by other things: a blast (17.2), a cannonball (17.3) and what a weapon does (the prizes: `SpecialObjects::weaponHit` and `burst`, one to three energy a hit by the projectile, 18.4).

| object (in the 83 levels) | the ball | a blast, a cannonball | a weapon |
|---|---|---|---|
| dynamite (116) | sets it off: 0.1 s later (its `exploseDelay`) it goes, 120 points | the same, one fixed step later | the same |
| cannon (21) | fires it, any touch; no damage | 2 energy: destroyed, 100 points | a bullet 1, a rocket 2, a kernel 3 (18.4); it does not fire it |
| firework crate (63) | launches a rocket, any touch; a blow destroys it too, 120 points (17.5) | destroyed, 120 points | destroyed |
| spring (21) | kicks it away; no damage | destroyed, 10 points | destroyed |
| volcano (13), generators (43), metal (452) | bounces off; they make their `hit` sound | destroyed, 10, 77 or 66 and 50 points | destroyed |
| teleports (22) | passes into a source (17.4), bounces off the rest | nothing | nothing |

**Why the ball does not hurt cannons, springs, volcanoes, generators and metal [M for the standing, I for the rule].**
- The whole wall of 83 Metall pieces of level 31 (`village-labirint`, the labyrinth of metal walls that the ball is in for the whole level) and an `int_cannon` in level 10 stood at the natural ends (second study, "Settled", 3), and the gift boxes of the generators stood through whole levels (2, 21, 22, 76).
- A volcano and a spring stood for 70 s of level 24 with a ball about (14.2), and the left cannon of level 51 fired twice and was "not consumed" (14.2).
- The designers' own levels need it: the metal fences of `village-apple` are walled off from the dynamite behind them except where a crate of `Box.jpg` (four energy) stops a gap, a weak place the ball breaks through (and the ball cannot get through metal).
- This replaces "a ball can still destroy them for their points" of the second study ("Settled", 3): they can be destroyed, by something else.
- The look at what the ball can reach (section 12) does not count metal as a wall, so nothing changes for the levels that can be won.
- A ball that bounces off one of them has still done nothing as far as the stall rule goes (section 12): its 90 s run on [I]. They are permanent now, so a ball can hit them for ever, and if that restarted the rule it would never end a level whose last target hangs out of reach: with every blow on them counted (tried), the autopilot's last target in `cowboys-banditos` (a bird 5 units up) kept the level open to 900 s, and 4 more levels stayed open besides it (57 of 83 finished, 8 of them by the stall rule, against 62 and 23). The cost: a player who works among metal for 90 s without hurting a thing, with at most 12 targets (or a quarter) left, has them let go.

**Why the firework crate is the exception [I].** It stood on the `Box.jpg` that stops each gap of `village-apple`'s two fences, and falls into the gap when the Box breaks. A crate the ball could not hurt (the first version of this section) plugged the gap for good: the autopilot stood at 117 of 118 targets left from 100 s to 900 s, and balls aimed at the gap turned back from the crate's face. A ball's blow takes the crate as it takes a brick, and the footage's brush that left a crate standing (V1 1013.33) was no blow (17.5). With that the autopilot, which does not aim, opens both gaps (the Box at 40 s and the crate at 51 s, the dynamite behind at 64 s, the second fence at 265 s and 306 s in a 600 s run); the level is slow for it, not shut.

**Why nothing hurts a teleport [M].** In level 67 (`knight-fruktovie-derevia`) the source stands 3.8 units from the nearest charge, which went off with its four neighbours, and a ball went into it again at V1 8213.7 (14.2). So a blast leaves a teleport; it is no brick (it is not a target, "Settled", 3), and `canBeDamaged` says no for both ends.

**How much a blast and a cannonball hurt [I].** 2 energy each (`blastDamage`, `cannonballDamage`): what has one or two energy (metal, dynamite, crates, cannons, the rest) is destroyed at once; a person of three is hurt for two, and lives (the log of V1 8192.9 was pushed and stood). The single blast is not what takes things in a chain: five charges 3.6 units apart overlap, and what is in the reach of several is struck by each. It is the footage's `+1933`: see 17.2.

### 17.2 Dynamite

- **Radius [D].** The level's `param`, the bits of a float: 5.0 on the 101 big charges, 3.0 on the 15 small (`GameObject::param`, `target`). A radius outside 0.5 to 30 is 5.
- **Goes off [M, D].** A ball hit takes the charge's one energy, and 0.1 s later (the material's `exploseDelay`; the footage's hit at 8192.76 and flash at 8192.83) it is destroyed: its 120 points, its `dynamite_dead` sound (two samples), and the blast.
- **The blast [I for the law, M for the numbers it must meet].** Every damageable object whose box is within the radius of the charge's centre of mass is struck `blastDamage` times; a charge it reaches goes off one fixed step (1/60 s) later, so a chain runs one link a step (the footage's five charges flashed over 0.07 s, V1 8192.83 to 8192.90: four links of 17 ms; its "20 to 30 ms" are frames of 33 ms). The reach, box to centre, comes to about 5.9 units centre to centre for a charge 1.8 wide, inside the bracket 4.0 to 6.9 (14.2); it stops at the gap of 6.9 in level 67. A loose body in reach is pushed away from the centre and up: a change of velocity of `blastSpeed` 14 units/s times the material's `explosePower` (1.0 for the dynamite [D]) times the nearness (1 at the centre, 0 at the edge), up to half as much again upward, and less for a heavy body (by the square root of 100 over its mass, so that a building is not thrown). The balls are not pushed (the rules set their speed).
- **Checked [M].** Replaying level 67: a hit to the charge at x = 18.2 sets off the five on the right, 17 ms apart, and not the five on the left; nine pieces of metal (450 points), ten stones (250), a dragon (360), the cauldron (70), a pointer (101), a flower (42), a log (50) and the five charges (600) make **1,923 points** within 0.32 s, against the footage's **+1,933** (+1,187 and +746 at V1 8192.80 and 8192.90, the other 1,333 "beyond the charges" of 14.7). The footage has the two steps 0.04 and 0.14 s after the touch at 8192.76 (frames of 33 ms); here the payments come between 0.10 s (the charge's own `exploseDelay`) and 0.32 s, the metal and stones after their own delays of 100 to 250 ms [?]. The same total comes out for a blast of 1, 2, 3 or 5 energy, since the neighbouring charges' blasts overlap and the damage adds: the footage does not say how much one blast does, only that it takes this.
- **The look.** The explosion file the level names (`explosebig` for the radius 5, `explosesmall` for 3: a flash of `Oreols/02.png` 25 and 15 across, [D]), swelling at its `speed` and fading over 0.5 s [D]; the particle system draws it (19.6). The sparks of the first version are gone: the footage has none (the orange fog and a bright core for each charge, `special-V1-t8192.76-8192.97`), and the data names none. The dynamite's own brick goes off with no flash of its own (19.5).
- **Not required [M].** A level ends with its dynamite standing (second study, "Settled", 3).

### 17.3 Interactive cannon

- **What fires it [M for the ball, I for the rest].** The ball touching it at any speed (the footage's cannons fired for a ball 1.5 to 2 units off, which is a graze), a loose body that hits it as hard as the ball's blow (V1 6446.85, F18), and a cannonball. At most one shot in 0.5 s. It never fires on a timer (none in 28 s).
- **What leaves it [D, M].** A cannonball of radius 0.59 [D, `yadro.XML`] from the muzzle, (1.99, 1.69, 0) in the cannon's frame, with the velocity (30, 3, 0) in that frame [D; measured 29.4 to 33.3 units/s]: along the cannon's own +x, whatever way the level turned it (the three of level 51 face +z).
- **Its flight [I].** A lob under the level's gravity: it falls 0.87 s after leaving a cannon on the floor and lands about 26 units on (the footage's tracked flights, 0.3 to 0.7 s, show no arc over 20 units, which this does not show either). It is swept by five rays (the middle and the rim) and bursts on the first actor it meets; balls and the bat it goes through; after 3 s it is gone.
- **What it does [M for the destroyed, I for the rest].** On impact the target is struck `cannonballDamage` (2) times, so what has one or two energy goes, a cannon of two included (a cannon is "not consumed" by the ball, 17.1), a charge it hits goes off at the next link (V1 6446.8: the chain went off, F17); a loose target is pushed (the two leave together: momentum is kept, the ball's mass 8.65 is its volume times `Telega1.jpg`'s density 10); a cannon it hits and does not destroy fires. A burst of `Oreols/11.png` 10 units across for 0.5 s [D, `exploseyadro`: the particle system's].
- **Hits to destroy [I].** The ball never does. A cannonball or a blast does at once (damage 2 against its energy 2), and a weapon takes two bullets, or a rocket or a kernel (18.4).
- **Not required [M].** `canBeDamaged` yes, a target no (second study, "Settled", 3).
- **Sound.** `brick_cannon_shoot` (`br_cannon.ogg`), at the muzzle; a short flare there and a puff of `Smoke` along the barrel (19.6).

### 17.4 Teleports

- **Entry [M].** A ball whose centre is in the source's trigger box (the one box of shape flags 15 of each of the 12 sources: 1.6 x 2 x 2, in the tunnel between its two plates) goes in: its body is taken out, it is no longer drawn, and the game goes on (it is no lost ball; the last ball in a teleport costs no heart).
- **Gone [M].** 0.2 s (0.13 to 0.25 seen). Then it appears at the exit's middle, growing from a speck in 0.1 s (3 frames), and drifts 0.2 s along the way it will leave (6 units/s; 0.7 units in the first 0.1 s seen), then leaves at the ball's speed, 15 [M: 15 to 17].
- **The way out [M for three cases, I for the law].** Toward the bat side, -z, at an angle drawn evenly from within 45 degrees of straight on (0, -14 and +45 degrees were seen); the draw is a seeded generator (`SpecialObjectRules::seed`), so a game repeats exactly. It does not follow the exit's orientation.
- **Pairs [I].** Two sources (`knight-zoo`, `village-dvorik2`) send their balls to the one exit; with equal numbers the k-th source goes to the k-th exit.
- **What enters [I].** Only the balls. Debris collides with the pyramid like anything else, and bullets and prizes (step 5's other half) are not asked.
- **Sound and look.** `teleport` at both ends; a ring on the floor and a glow where it goes in and where it comes out.
- **Not required, and nothing hurts them [M].** 17.1.

### 17.5 Firework crates

- **A touch [M for one, I for any speed].** The ball touching the crate at any speed (V1 1013.33 was a brush) starts a rocket that comes off the crate **0.7 s** later, from its top (3.7 units above the crate's origin: 6.5 units at 0.18 s), rises straight up at **15.6 units/s** and bursts **1.2 s** after it left (about 22 units up; the data's `lifeTime` of 2.5 s is not what the footage shows). A touch while a rocket is coming or up starts no other; after the burst the next touch starts the next. It is never on a timer [I: a crate stood for whole levels with nothing]. One rocket a touch: the small second rocket of F1 is not built.
- **No score for the rocket [M].** The launch scores nothing and the rocket and the burst hurt nothing.
- **A touch that is no blow leaves the crate standing [M]; a blow destroys it [I].** The footage's crate stayed after a brush (V1 1013.33). In the game that is a ball whose edge goes no deeper than about 0.05 units into the crate's side: its closing speed along the contact is under a blow's (`hitFraction`, section 5). Any other hit is a blow and takes the crate's one energy as it does a brick's: after the material's 100 ms it is destroyed for its 120 points (a person's `exploseType` 5: it shrinks away). The footage does not say whether a harder hit kills a crate [?]; the first version of this section said never, and `village-apple` could not be won (17.1).
- **A lit crate fires [I].** The rocket is lit by the touch, so a blow that destroys the crate does not stop it: the rocket comes 0.7 s later from where the crate stood, and so does one whose crate a blast took in those 0.7 s. A crate that a blast, a cannonball or a weapon destroys without the ball having touched it gives no rocket.
- **The look.** The rocket (`Fire-rocket.x`) with the `TrailOrange` trail from its tail and a shadow on the floor; the burst, 30 sparks flying out and slowing in one of the six of `Fireworks.xml` [D], for about 0.7 s [M], mostly above the picture's top edge for a crate at the back, as in the footage. The particle system draws the trail and the burst (19.6). The sound is `firerocket_dead` at the burst.

### 17.6 Springs, volcanoes, generators and metal

- **Spring [I].** A ball that hits it with a blow leaves **away from its middle**, along the line from the spring's centre to the ball's, at the ball's speed (the pinball bumper: the kick is the direction, since the speed is always 15); the spring gives for 0.25 s (it swells a fifth and settles) and makes its `spring_hit`. The ball does not hurt it, a blast does (10 points). The one clean hit the footage might have had (V1 2634.0, 2.6 units off) was not a hit. Its `loop` sound is not played.
- **Volcano and generators [M, weak].** Idle: no eruption (70 s), no prize (10 to 38 s). The ball bounces off them and they make their `hit` sound; a blast takes them for 10 (volcano), 77 (good) and 66 (bad). The generators give no prizes in this step (their rate is not in the data).
- **Metal [M for the standing, I for the rest].** Bounces the ball by its material's own numbers (restitution 0.1, friction 0.999 [D]) and makes `metall_hit`; the ball does not hurt it (17.1); a blast, a cannonball or a weapon destroys it for 50 points (the footage's chain took a fence in one frame).

### 17.7 Vibros and flickers

- **Vibro [M].** The barrels and rings (kinematic, 23) bob and tilt in place: the body goes up and down by 0.1 unit and tilts 6 degrees [I] about its own origin with a period of 2.4 s, each with a phase of its own, so that the ball is pushed by what it sees. The ball destroys one like a brick (14 to 24 points); it is not required.
- **Flicker [M, I].** The cauldron and the torches (14) are bricks of the ball's kind (70 points for the cauldron, `exploseDelay` 150). The light "around them changes by 1.4 %" (F13) and the renderer has one directional light, so the point light is left out; each gets a warm glow sprite that wavers (two sines of unrelated rates, a phase of its own), and goes with it. The particle files they name burn too: sparks (`Spark` for a torch, `SparkKoster` for the cauldron) and `Smoke` rise from the top of the fire (19.6).

### 17.8 The open questions of 14.7

| question | what the game does | tag |
|---|---|---|
| Spring: kick or bounce, one hit destroys? | kicks away from its middle at the ball's speed; the ball does not destroy it | I |
| Cannon: hits to destroy; the cannonball's damage and range; a slow ball or a bat shot; a timer | the ball never; a cannonball or a blast at once; damage 2; a lob of about 26 units; any touch fires it, however slow (the ball is never slow, and the bat is behind the line); no timer | I |
| Teleports: the exit angle; prizes, debris, bullets; which exit two sources share | drawn evenly within 45 degrees of straight on toward -z; only balls; the one exit | I |
| Dynamite: the reach; small dynamite; the +1333 | box to centre within the radius (about 5.9 centre to centre for 5); the same rule for 3; replayed: 1,923 against 1,933 (17.2) | I, M |
| Firework: launch by a touch, a timer or a delay; rockets a crate; does a harder hit destroy it | by a touch, 0.7 s later; one a touch, one at a time; yes: a blow does (120 points) and the rocket it lit comes anyway, a brush does not; so do blasts, cannonballs and weapons | I |
| Volcano and generators: any eruption; can the ball destroy them | none and no; blasts do | M, I |
| Metal: how the ball bounces; can it destroy it | by its material; no | D, I |
| Cloth and trees | cloth unchanged: not built here; a tree that is hit sheds its leaves (`LeafFall`, 19.6) | I |

### 17.9 The numbers (`SpecialObjectRules`)

| constant | value | tag |
|---|---|---|
| `blastDamage`, `cannonballDamage` | 2, 2 | I |
| `defaultBlastRadius` | 5 (the level gives 5.0 or 3.0) | D |
| `chainDelay` | 1/60 s | M |
| `blastSpeed`, `blastLift`, `blastReferenceMass` | 14 units/s, 0.5, 100 | I |
| `shotSpeed`, `shotLift`, `muzzle`, `shotRadius` | 30, 3, (1.99, 1.69, 0), 0.59 | D (M: 29.4 to 33.3) |
| `shotMass`, `shotLife`, `cannonCooldown` | 8.65, 3 s, 0.5 s | D, I, I |
| `portalGone`, `portalGrow`, `portalDrift`, `portalDriftSpeed`, `portalSpreadDegrees` | 0.2 s, 0.1 s, 0.2 s, 6 units/s, 45 | M, M, M, M, I |
| `rocketDelay`, `rocketSpeed`, `rocketLife`, `rocketStartHeight` | 0.7 s, 15.6 units/s, 1.2 s, 3.7 | M |
| `vibroPeriod`, `vibroBob`, `vibroTiltDegrees` | 2.4 s, 0.1, 6 | M, M, I |

What is tested: `mb_special_object_tests` (made-up levels: the blast, its reach and radius, a chain that stops at a gap, 25 charges in a block, what the ball does not hurt and a blast takes, the cannon in two orientations with its flight, cooldown and debris, the teleports with their timing, angle, seed and shared exit, the crate's rocket after a brush and after a blow, the crate that stands in a gap, the stall rule among metal, the spring's kick, the vibro's period, the end of a level with all of it standing) and `mb_special_object_smoke` (the 71 real levels with such objects, played by the autopilot and poked: every charge, cannon, crate, spring and source; no pose or ball that is not finite, a chain that ends, every ball that went into a teleport comes out, every rocket bursts).

## 18. Weapons and strikes as built (step 5, weapons)

What the game does with the four weapons and the two strike prizes of sections 7 and 14.3 where the footage and the data leave room: the rules, with no picture and no sound (the barrels, the bullets, the beam, the lightning, the meteors and their sounds are the next stage's). Tags as before; `[I]` is a choice made where both are silent. The rules are `Weapons` (`game/Weapons.h`, `game/src/Weapons.cpp`), a member of `GameSession` that is its friend, called at three places of `Gameplay.cpp` and `SessionPrizes.cpp`: a click that no ball takes (`fire`), the catch of a Thunderbolt or a Meteorite Rain (`startStrike`) and once a fixed step after the contacts (`update`). Its numbers are `WeaponRules`, `GameplayRules::weapons` (18.10). The projectiles are bodies of the session's `PhysicsScene` (`addProjectile`); what they do to an object goes through `SpecialObjects::weaponHit` and `burst`, so that 17.1 stands. Tests: `mb_weapon_tests` (made-up levels), `mb_weapon_smoke` (the 83 levels), `mb_gameplay_smoke` (the autopilot fires what it has caught).

### 18.1 Firing

| rule | in the game |
|---|---|
| the trigger | a click (`GameInput::launch`: the mouse button going down, or Space) fires **one shot** of `weapon()`, the weapon caught last that has shots left; no auto-repeat. The footage's counts fall at irregular moments, so a press is the guess [M, I] |
| what else a click does | one thing, in this order: it launches the ball held on the bat; else lets go of the balls the Magnet holds; else fires. A ball held on the bat is waiting for the click (section 4), and the Magnet's release is a click (15.3), so each keeps it, and the weapon needs one more: with both, the first click frees the balls and the second shoots [I] |
| when | only with balls in play (`SessionPhase::Playing`) and the bat there. Not while the bat swoops in with the ball on it (`Entering`) or the ball waits (`Ready`), not after the last ball is lost (`Lost`: a weapon caught in those seconds does not fire either) and not once the level is won or the game over [I] |
| the shot count | falls by one at the shot (`ShotFired`, then `GameSession::useShot()`), and the weapon's effect ends with the last (`EffectEnded` follows) [M]. A click that fires nothing (too soon, nothing armed, the world refused the body) costs no shot |
| the interval | at least `shotInterval` = 0.2 s between two shots, whatever the weapon: the Machine Gun's own quickest, 4 to 5 shots a second [M, weak]; the others showed none (their quickest were 0.55 s, the Cannon, and 0.5 s, the Laser), so they share it [I] |
| a new weapon | `weapon()` is the one caught last. **The earlier ones are not lost**: they keep their shots and fire again when the later one is spent, as the footage's HUD shows (the Cannon with 1 shot beside the Bazooka's 7, 6, 5, 3: V2 108-117, `special-V2-t108-117-hud-cannon-1-left-drunken-30s-bazooka-7-6-5-3`); a second catch of the same weapon adds its shots and makes it the weapon again. That is what `startEffect` (15.3) already did; `mb_weapon_tests` checks it |
| where from | the barrels are frames of the bat's model, whose origin is on the floor under the centre of the bat's sphere and which is drawn `batScale()` times its size (16.3): a shot starts at the data's spawn point in that frame (x across, y up from the floor, z up the board), scaled with the bat [D for the points, I for the scale]. The bat's own speed is not added to the shot's [I] |

### 18.2 Projectiles

- Each is a **dynamic body** in the session's `PhysicsScene` (`PhysicsScene::addProjectile`): one sphere or capsule, continuous collision, never asleep, not turning, with gravity only where the data has no `disableGravity`. A capsule lies along z, the way it flies (the data's rotations turn its axis to z). The material is the data's [D]: density 5, friction 0.7, restitution 0.3 for the four `bat_*` materials, and 10, 0.9, 0.5 for `thunderbolt` and `meteor`; the mass is the volume times the density (a bullet 2.5, a rocket 5.1, a kernel 13.7, a bolt or a meteor 5.0).
- **What it touches [I]**: the level's actors, nothing else. Not the balls, the bat, other projectiles or the teleports, and the prizes are no bodies. In the engine that is `BodyDesc::group` and `collidesWith` (a body touches another only when each one's `collidesWith` has the other's `group`; the defaults touch everything): the kinds are `kKindLevel`, `kKindBall`, `kKindBat`, `kKindProjectile` and `kKindPortal` (`PhysicsScene.h`), and the teleports are made `kKindPortal` that leaves `kKindProjectile` out (17.8: "only balls").
- **Its first touch of an actor ends it.** The projectile is taken out of the world in that step and reports `ProjectileHit` (`BoltStruck`, `MeteorImpact`) with the actor and the contact point; the impulse of the contact has by then pushed what it met as its momentum says (a bullet of 2.5 at 30 sends a loose block of 30 off at about 3 units/s). A projectile that touches nothing is gone after `projectileLife` = 5 s, or when it is beyond +-80 or below -20, with `ProjectileGone`; the board's walls and floor are actors, so every shot ends in a hit in a closed level. A hit that begins at more than a joint's `maxForce` breaks it as any impact does (`JointBreaking::Impact`), with the projectile's own mass.
- What ends the level ends them: once `LevelComplete` or `GameOver` comes, every projectile in the air is taken out (`ProjectileGone`) and no strike is thrown.

### 18.3 The four weapons

| weapon | shots | what leaves the bat (the bat's frame) | flies |
|---|---|---|---|
| Machine Gun | 12 | **two bullets** at x +-2.156, y 1.198, z 3.411, capsules of radius 0.4 and length 0.44 [D] | (0, 0, 30), no gravity [D]: 30 to 37 units/s measured [M]; to the far wall in about 1.3 s |
| Cannon | 6 | one kernel, a sphere of radius 0.868 at (0, 1.704, 3.067) [D] | (0, 3, 40) **with gravity** [D] (-9.8): a lob that comes down on level ground after 0.82 s, about 15 units up the board, so it cannot reach the last 7 units of a board unless it hits something tall on the way [I from D]; the footage's 42.6 units/s is the part along z [M] |
| Bazooka | 7 | one rocket, a capsule of radius 0.452 and length 1.0 at (0.006, 1.406, 4.791) [D] | (0, 0, 52.5), no gravity [D]; 54.5 measured [M]; to the far wall in 0.75 s |
| Laser | 5 | a beam, no body (below) from the barrel at (0, 1.0, 2.91) [D] | the beam is full length within 0.1 s [M] |

**The Laser's rule [I].** The footage has a thin beam that is full length within 0.1 s and fades in about 0.45 s (V2 62.78-63.26), that killed planks with the ball far away (V1 59.25, 90.2 to 91.2) and two of them at once (+34 and +34, V1 59.2-59.37), and a pirate of three energy that survived one (V2 62.9). Hence:
- the beam is **resolved at the shot**, a line along +z from the barrel, three rays 0.15 apart (the data's capsules have the radius 0.15), at the height of the barrel;
- it **pierces**: every actor it crosses that something can hurt (`canBeDamaged`) takes `laserDamage` = 1 energy, **once**, however many shapes of it the beam crosses: planks (one energy) die, a pirate (three) is left with two;
- it **stops at the first actor that nothing damages** (a wall, the board, a solid piece of scenery, steel), at the point where it meets it (`LaserBeam::to`); with nothing in the way it reaches `laserRange` = 60. In the real levels that is the far wall or a platform;
- the balls and the bat are no actors, and the **teleports are not there**: it goes through them;
- it pushes nothing.
`beams()` has it on show for `laserLife` = 0.45 s, growing to its length within `laserGrow` = 0.1 s [M] (what the picture does with the numbers is the next stage's).

### 18.4 What a hit does

Per projectile, the energy it takes off what it hits and its burst (`WeaponRules`, 18.10). **Decided [I], from the footage where it speaks:**

| projectile | takes off the actor it hits | burst on impact |
|---|---|---|
| Machine Gun bullet | 1 | none |
| Cannon kernel | 3 | radius 2.0: 1 off everything else damageable within it, and a push |
| Bazooka rocket | 2 | radius 2.25: 1 off the rest within it, and a push |
| Laser beam | 1, to each actor it crosses | none |
| Thunderbolt's bolt | 1 | none |
| Meteor | 2 | radius 2.0: 1 off the rest, and a push |

- The footage fixes the least: a beam leaves a pirate of three alive [M] and a bolt does not kill one either: the pirate of V2 83.6-86.4 lived through a bolt and a blast and died to the next bolt, which is 1 + 1 + 1 [M, weak]. The order bullet, rocket, kernel (1, 2, 3) is [I]: the Cannon has 6 shots and a lob, the Bazooka 7 and a straight shot, the Machine Gun 12 shots of two bullets; the meteor takes two as a heavy thing falling from the sky.
- **Bursts.** The rocket's is the footage's white-orange star 4 to 5 units wide [M], radius 2.25. The kernel's flash is about 3 units wide in the footage (F48), and the data's `exploseyadro` is 10 across, which is a radius of 2 for the dynamite's five-times rule (17.2); the rule takes the data's. The meteor's `explosemeteor` is 10 across too. A burst takes `blastDamage` = 1 off every damageable actor within its radius, the distance to the box round it, as the Explosive Ball does (15.3), but the one the projectile hit; a charge of dynamite in reach goes off at the next link (17.2). It pushes the loose bodies in reach away from its centre and up, as dynamite does at `blastPower` = 0.5 of its strength (up to 7 units/s at the centre).
- **Through `SpecialObjects` (17.1)**, so nothing is done twice: `weaponHit(actor, at, damage, dropsPrize)` takes `damage` energy one at a time, `burst(struck, centre, radius, power, damage, dropsPrize)` is a burst, the law of the dynamite's blast. So **dynamite is set off by any projectile**, 0.1 s later (its `exploseDelay`) as for the ball, and its chain runs as in 17.2; a **cannon** of two energy takes two bullets, a rocket or a kernel, and is not set off by them; a **firework crate** is destroyed (120 points) and gives no rocket, since the ball did not light it (17.5); **springs, volcanoes, generators and metal** are destroyed for their points; the **teleports** are not damageable and the projectiles pass them; bricks and trees take the energy and a person or an animal shrinks away as ever. An object that is already exploding, shrinking or gone is not hurt again, and a projectile that meets one is spent on it all the same.
- **Prizes: the data decides [D].** `disablePrize` is on `thunderbolt.XML` and `meteor.XML` and on none of the four weapons' bullets, so what a bullet, a kernel, a rocket or a beam destroys leaves a prize at the usual chance (15.1), and what a bolt or a meteor destroys leaves none (`GameSession::hit(actor, at, false)`). V2 81.0-81.5 saw a prize orb where a bolt struck, which does not fit; it stays open (14.7). The score is the destroyed object's own `brickScore`, as for anything the ball destroys.

### 18.5 Thunderbolt

| rule | in the game |
|---|---|
| a catch | `StrikeGranted`, then a **burst of 2 or 3 bolts** (drawn), thrown `boltGap` = 0.2 s apart from the catch, so that they strike one after another within 0.7 s [M], and `boltsLater` = 2 **more**, each at a moment drawn evenly between 2 and 12 s after the catch [M, weak: bolts at 8 and 12 s in V2, at 0.5 s and 7 s after the caption in V1 level 2]. A second catch adds another set |
| the target | picked at random, at the moment the bolt is thrown, among the **standing bricks and trees**: damageable, and not gone, exploding or shrinking; whatever the level's end rule, and out of the ball's reach or not. Metal, cannons and the rest are spared (the Metall wall of V1 level 31 stood through five bolts, 14.2). With none left, a point of the field drawn evenly [I] |
| the bolt | a sphere of radius 0.492 that **falls at 50 units/s** [D], straight down, from the top edge of the screen above the target's centre (18.7), so it falls 15 to 30 units: 0.3 to 0.6 s. It strikes the first actor under it, in practice the top of the target or of what stands over it, and takes one energy. It is a physics body like the rest, so it strikes whatever is under it when it arrives, which is not the target if that has moved [I] |
| what it reports | `BoltStruck`: the actor, where it struck, and `end`, the point of the screen's top edge above it, where the picture draws the lightning from. If what it struck reaches above the top edge (a tall tower), `end` is below `position`: draw nothing, it is out of the picture |
| prizes | none (disablePrize [D]) |
| the seeds | `SessionOptions::prizeSeed` turned by a constant, SplitMix64, so that a game repeats exactly |

### 18.6 Meteorite Rain

About five meteors over five seconds [M] (rings at V2 860.5, 861.3, 863.1, 864.3): `meteors` = 5, one in each fifth of `meteorSpan` = 5 s, at a moment drawn within it. Each one:
- aims at a **point of the field drawn evenly** (x +-18, z -10 to 20), since the footage shows the rings on the water and no brick [M, I], so the ones that happen to fall on something hurt it. The aim is where the sphere touches the water; the meteor flies along the line through that point with the velocity (-10, -30, 10) [D] and no gravity;
- starts where that line crosses the top edge of the screen (18.7), a little beyond it, so it falls 20 to 30 units, 0.7 to 1.0 s at 33 units/s;
- hurts what it touches first: 2 energy, then the burst of radius 2 that takes 1 off the rest. `MeteorImpact` has the actor: the water is the board's own actor, and the ring on it is the picture's;
- leaves no prize (disablePrize [D]).

### 18.7 Where from: the top edge of the screen

The camera never moves (section 8: eye (0, 42.7, -58.5), target (0, 0, 2.7), vertical FOV 32.8 degrees), and the vertical FOV is fixed whatever the window's shape, so the top edge of the picture does not depend on the aspect. `screenTopHeight(below)` is the height at which the vertical line through a point leaves the picture at the top: **28 units up over the near end of the field (z -15), 23 over the middle (z 0), 16 over the far end (z 20)**. `screenTopPoint(target, direction)` is the point on the line through `target` with a direction (a meteor's) that is on the top edge. A bolt starts `offscreen` = 2 units above its edge, a meteor 2 units along its line beyond it, so neither is ever seen to appear. `mb_weapon_tests` checks both against the camera's own view and projection (NDC y of 1).

### 18.8 What the presentation reads

**State** (`GameSession::weapons()`, `projectiles()`, `laserBeams()`; a span like `fallingPrizes()`):
- `Projectile`: `id`, `kind` (`Bullet`, `Kernel`, `Rocket`, `Bolt`, `Meteor`), `prize` (the weapon, or Thunderbolt or Meteor), `position` (its centre), `velocity`, `bornAt`, `age`, `barrel` (the Machine Gun's: 0 left, 1 right), `body`. Positions and velocities are those of the last fixed step; the picture carries them on by the lead as it does the balls'. In the order they left; at most a few dozen at a time (the smoke bounds it at 128).
- `LaserBeam`: `from`, `to`, `firedAt`, `hits` (the actors it reached, the one that stopped it included). Gone after `laserLife`.
- `Weapons::strikesPending()`, `Weapons::stats()` (shots, projectiles, hits, gone, beams, bolts, meteors, hurt).
- Already there: `weapon()`, `shotsLeft()`, `activeEffects()` (the HUD's shot numbers), `batScale()`.

**Events** (`GameEvent`, with the two new fields `end` and `count`; `material` is the projectile's game material: its `periodSound` is the shot's sound and its `deadSound` the impact's, `bat_bullet`, `bat_kernel`, `bat_rocket`, `bat_lazer`, `thunderbolt`, `meteor`):

| event | fields |
|---|---|
| `ShotFired` | `prize` (the weapon), `position` (the muzzle: the middle of the barrels), `velocity` (of a projectile; none for the laser), `count` (shots left), `material`. One a shot, so one sound for the Machine Gun's pair |
| `ProjectileHit` | `prize`, `actor` (what it touched: the board and what nothing damages too), `position`, `velocity` (its own), `material`. The laser reports one for each actor it reached |
| `ProjectileExploded` | `prize`, `position`, `speed` (the burst's radius). Kernel and rocket, after their hit |
| `ProjectileGone` | `prize`, `position`: it left the world, ran out of time, or the level ended |
| `LaserBeam` | `position` (the barrel), `end` (the tip), `count` (the actors it reached), `material`. Before its `ProjectileHit`s and the damage |
| `BoltStruck` | `actor`, `position` (where), `end` (the top edge above), `material` |
| `MeteorFalling` | `position` (where it starts), `velocity`, `end` (where it will come down, on the water), `material`: at its start |
| `MeteorImpact` | `actor` (the board's for the water), `position`, `material`, then its `ProjectileExploded` |

Every projectile ends with exactly one of `ProjectileHit`, `BoltStruck`, `MeteorImpact` or `ProjectileGone`. The damage is the usual `BrickHit` (`ball` -1) and `BrickDestroyed`, and the prize drops the usual `PrizeDropped`.

### 18.9 The open questions of 14.7 that were about the weapons

| question | what the game does | tag |
|---|---|---|
| hits needed against energy 2 to 4 for the cannonball, the rocket, the bullets, the laser | a bullet 1 (a brick of 4 takes 4), a rocket 2, a kernel 3, a beam 1 to each actor in line; bursts 1 more to what is within reach | I |
| the blast radii of the cannonball and the rocket | 2.0 and 2.25 | D, M |
| a minimum interval between shots | 0.2 s for all | M for the gun, I |
| Thunderbolt: strikes per catch, their span, the target | 2 or 3 at once and 2 more up to 12 s later; a random standing brick or tree | M weak, I |
| Meteor Rain: count, targets, damage | 5 over 5 s, random points of the field, 2 energy and a burst | M, I |
| does a weapon push | by its momentum, bursts by 0.5 of dynamite's | I |
| do weapon kills leave prizes | yes, but not the bolt's and the meteor's | D |
| do teleports, balls and the bat stop projectiles | no | I |

### 18.10 The numbers (`WeaponRules`)

| constant | value | tag |
|---|---|---|
| `shotInterval` | 0.2 s | M (gun), I |
| `bullet`, `kernel`, `rocket`: `spawn`, `velocity`, `gravity`, `radius`, `halfHeight`, material | the data of 18.3 | D |
| damage: bullet, kernel, rocket, laser, bolt, meteor | 1, 3, 2, 1, 1, 2 | I |
| burst: kernel, rocket, meteor (radius; damage 1, power 0.5) | 2.0, 2.25, 2.0 | D, M, D |
| `laserSpawn`, `laserRadius`, `laserRange`, `laserGrow`, `laserLife` | (0, 1.0, 2.91), 0.15, 60, 0.1 s, 0.45 s | D, D, I, M, M |
| `projectileLife`, `farLimit`, `lowLimit` | 5 s, 80, -20 | I |
| `bolt`: velocity, radius | (0, -50, 0), 0.492 | D |
| `boltBurstLeast`, `boltBurstMost`, `boltGap`, `boltsLater`, `boltLaterFrom`, `boltLaterUntil` | 2, 3, 0.2 s, 2, 2 s, 12 s | M, M, I, M weak, I, M weak |
| `meteor`: velocity, radius | (-10, -30, 10), 0.492 | D |
| `meteors`, `meteorSpan`, `meteorX`, `meteorZLow`, `meteorZHigh` | 5, 5 s, 18, -10, 20 | M, M, I |
| `offscreen` | 2 units | I |

What is tested: `mb_weapon_tests` (made-up levels: the engine's kinds of body and `raycastAll`; the top edge against the camera; the click with no weapon, with a held ball, while the bat swoops in, with the Magnet, in the Lost phase and after the level is won; the shot count, the interval, a weapon running out and replacing another; each weapon's spawn point, velocity and gravity, and the barrels of a wider bat; a projectile that leaves the board; the damage of each projectile against bricks of one to four energy, the burst's reach and push, what is spent on steel and walls; the laser's piercing, width, stop, one hit a body, teleports and balls, fading and range; dynamite set off by a bullet and its chain, a cannon, a firework crate, metal, springs, volcanoes, generators and teleports; the prizes a kill may leave; the Thunderbolt's burst, spread, targets, top edge, metal spared and seeds; the meteors' count, timing, slant, aim and damage; the strikes' damage; that nothing is thrown once the level is won; the autopilot's look ahead; two identical games) and `mb_weapon_smoke` (the 83 levels, every weapon and both strikes given at once, the click held down, 24 s each: nothing not finite or off the board, shots counted, every projectile accounted for, the strikes all thrown, the balls' speed and plane, the time of a step).

## 19. Particles as built (step 5, fx)

The particle files of `Data/Particles` as the game draws them: the explosions' flashes, the firework bursts and the rocket's trail, the smoke and sparks of the flickers, the leaves of a tree that is hit, and the beams and bolts the weapons will need. What the files say is `OBJECTS.md` section 8 (**data**); what they mean is **inferred**, since the original's code is not at hand, and every reading below is tagged **[I]** where it is a choice, **[M]** where a frame of the footage (`reference/gameplay`) bears it out, **[D]** where it is the data's own number. The code is `game/ParticleDef.h` (the files), `game/ParticleSystem.h` (the simulation), `game/ParticleDraw.h` (the batches), `game/BrickEffects.h` (the table of 19.5) and, in the engine, `EffectBatch` (`Renderer.h`, `ARCHITECTURE.md` "Particles"). `PlayScene` owns the one system and steps and draws it; `ObjectEffects` and the weapons ask it for effects.

### 19.1 The model

| rule | in the game |
|---|---|
| definitions | `ParticleLibrary::load` reads every `*.json` of `Data/Particles` (20 files, 28 definitions); a definition is named `file/name` (`Fireworks/red`) or by its file alone (`explosebig`, the file's first), any case. It reports what it cannot read (`problems()`) and each attribute it does not know (`unknownAttributes()`): none, for the real files |
| one effect, many particles | a particle's whole state is a function of its age, its start and a few fixed random numbers (no integration), so a run repeats from its seed (`EffectOptions::seed`, else the system's own sequence) and the tests can ask for any moment |
| how long a particle lives | Flash, Trail, Laser, Lightning: `fade_time_msec`; Smoke, LeafFall, Tornado: `height` / `speed`; Firework: 2 x its reach / `speed`; WaterBubble: 1.5 s (no file gives a height) [I] |
| emitters | Trail, Smoke, WaterBubble and Tornado keep `num` particles alive: `num` / life of them are put out each second. `attach()` runs until `stop()` (`moveTo()` says where it is, and the particles of a frame are spread along the way it moved, so that a rocket at 15.6 units/s leaves an even line however long the frame); `spawn()` with `seconds` runs that long, and with none puts all `num` out at once |
| bursts | Flash (one sprite), Firework (`num` sparks) and LeafFall (`num` leaves) go off at once |
| scale | `EffectOptions::scale` multiplies the sizes and the distances a definition gives, not the times (a Tornado at a tenth of its 160 units fits a board) |
| beams | `beam()` makes a Laser or a Lightning between two points; it stays at full strength for `seconds` after it is made and after each `aim()` (so one aimed every frame never fades) and then fades over `fade_time_msec` |
| the halos | the Oreol files are the prizes' halos, which `PrizeScene` draws (16.1): the system parses them and does not draw them (`spawn` gives no effect) |
| drawing | every particle is a quad (a camera-facing sprite, a flat mark on the floor, or a strip) in world space; the quads go to the renderer in a handful of batches, one for each texture and way of blending, drawn after the meshes with no depth write (`ParticleBatches`). The night dims them like the other things with no light (0.49 at full night, 14.3) |

### 19.2 The ten types

| type | what the file gives | what is drawn [I unless marked] |
|---|---|---|
| Flash | `size`, `speed`, `fade_time_msec`, a `direction` on `explosemeteor` | one sprite turned at random about the line of sight that swells at `speed` (units a second) to `size` and fades in a straight line over `fade_time_msec`. 25 for the dynamite's radius 5 is 0.1 s of swelling at 250: the 0.08 s of 17.2 [D, M]. With a `direction` it lies flat across it, 0.1 up, tested against the depth: `explosemeteor`'s ring on the floor spreads at 50 units/s and is gone in 0.2 s, as the white rings of `special-V2-t858-865` [M] |
| Firework | 30 sparks, `radius` 10, `speed` 30, `size` 3.5, 500 ms, `direction` down, two colours | each spark flies out along a direction evenly over the sphere to a reach between half and all of `radius`, fast at first and at rest at its reach (it leaves at `speed`, so it lives 2 x reach / `speed`, 0.33 to 0.67 s: the footage's burst is gone by 0.6 s [M]), droops along `direction` at 5 units/s^2, shrinks to half and fades over the last 500 ms, and runs from `colour1` to `colour2` |
| Trail | 30, 500 ms, `size` 2.5 or 5, `radius` 0 or 1, two colours | sprites left where the emitter was (scattered in a sphere of `radius`), standing still: they shrink to 0.4 of their size and fade in a straight line, running from `colour1` (hot) to `colour2` |
| Laser | `speed` 150 or 0, `size` 1, 1.5 s or 1 s, two colours | a strip `size` wide between the points, the middle tile of `ParticleLaser1.png` with a round end at each end, the front running out from the first point at `speed` (0: all at once), `colour1` at the first end to `colour2` at the far end, fading over `fade_time_msec` |
| Lightning | 10 pieces, `shift_coeff` 0.5, `shake_coeff` 0.75, `size` 1, one colour | ten strips from point to point, the nine points between them thrown aside by up to half a piece; three quarters of the throw is drawn anew 30 times a second (the bolt crackles), a quarter is fixed; a round end at each point |
| Smoke | 50, `angle` 10.5, `speed` 2, `height` 3, `width` 0.1, `size` 1.5, 500 ms | puffs from a source 0.1 wide rise in a cone of 10.5 degrees at 0.7 to 1 times `speed`, for `height` / `speed` = 1.5 s, swelling from 0.4 of their size to the whole, at 0.4 opacity, fading over the last 500 ms |
| WaterBubble | 10, `speed` 1 or 2, `radius` 0.5, 2 or 8, `size` 1 or 1.5, `direction` up | sprites rise from a disc of `radius` at `speed` for 1.5 s, swaying 0.15 to either side; `fade_time_msec` (50 or 500 ms) is how long they fade in and out |
| LeafFall | 15, `dimension` 3 x 2 x 3, `height` 6, `speed` 2, `radius` 5, `angle` 25, `size` 1.5 | the leaves start in the box; each falls at 0.7 to 1.3 times `speed` (3 s for the whole `height`), drifts up to `radius` aside, rocks by up to `angle` degrees about the line of sight, never goes below 0.15, and fades over the last 0.5 s |
| Tornado | 1,500, `radius_small` 5, `radius_big` 50, `height` 160, `speed` 25, `speed_angular` 4.5, `size` 20, 1.5 s | particles rise up the column at `speed` (6.4 s) while they turn about it at `speed_angular`, on a radius that grows from `radius_small` to `radius_big`; they fade in and out over 1.5 s at 0.35 opacity. Its texture is missing (`Particles/PARTICLE.PNG`, a soft dot, stands in) and nothing names it |
| Oreol | `speed_angular`, `radius`, `radius_offset`, `speed`, `shadow_plane` | the halo of a prize: `PrizeScene`'s (16.1) |

### 19.3 Colours, blending and the textures

- **Colour is the texture's alpha and the file's colour.** A definition with colours (Firework, Laser, Lightning, Trail) draws its colour in place of the texture's, whose alpha shapes the sprite (`EffectBatch::colourOnly`). The files that say so say `modulate_colour="no"`; Fireworks say nothing and are read the same way, because they use the orange dot of `ParticleTrail(orange).png` for all six colours, which a blue spark cannot be if the dot's orange is multiplied in, and the footage's burst is pink [I, M: `special-V1-t1014.20-1015.52`]. The alpha byte of a colour (`0x00...` on the trails and the bolt) is left out: it is not "invisible" [I].
- **`colour1` is where it begins, `colour2` where it ends** (a trail ages from one to the other, a spark too, a beam runs from its first point to its far end): the yo-yo's string is green at the bat and blue at the ball in the footage [M, 16.3], and a flame cools from white to orange [I].
- **Additive** for the sparks and the glows that are light on a dark ground: Firework, Trail, Laser, Lightning. **Alpha blended** for Flash, Smoke, LeafFall, WaterBubble and Tornado [I]. The flash is the one that matters: the dynamite's fog is solid orange over green grass in the footage (`special-V1-t8192.76-8192.97`, frames 3 to 6), which an additive flash cannot make (it saturates to white where five overlap), and the shark's small flash is an orange that stays orange on the blue water [M].
- **The textures are drawn as they are.** Five (`ParticleTrail(orange)`, `Particlered`, `PARTICLE`, `ParticleBubble`, `Bubble2`) have their dot below and right of the middle, by up to a fifth of the sprite: it shows as a slight shift of the rocket's trail to the right, and nowhere else [I: the original's art and, we suppose, its look].

### 19.4 Sizes the footage checks

The dynamite's blast: five flashes of `explosebig` overlap into an orange fog with a bright core each, as in the six frames of `special-V1-t8192.76-8192.97` [M]. The shark's explosion (`exploseType` 3, `exploseball`: 10 across) is a small orange flash on the dark fish [M: `chillcalex-...-t54.1-54.5`]; a crate's (6) is the same size or a little more [M: `rules-V1-t3580.7-3581.4-L30`]. The firework's burst (`special-V1-t1014.20-1015.52`): pink sparks that spread over about 0.3 s and are gone by 0.6 s, mostly above the picture's top edge for a crate at the back [M]. The cauldron's smoke (`special-V1-t3011-3015.8`) is a thin grey wisp over the pot [M, weak].

### 19.5 What breaks in what (the material's `exploseType`)

The data links no explosion file to a type (`OBJECTS.md` 6 and 9); this is the game's table (`explosionEffects`, `game/BrickEffects.h`), a choice [I]:

| `exploseType` | what it is | effect |
|---|---|---|
| 0, 2 | system, board, light, Default, ball, bullets | none |
| 3 | prizes, the bat's bullets, the ships' hulls and sails, the shark, a few more | `exploseball`: the small flash, 10 across |
| 5 | people, animals, trees, fruit, the interactive objects | none: they shrink away, and the footage has no flash on a pirate (section 5, 8) [M] |
| 6 | what has splinters: buildings, ground, crates, metal | `explosesmall`: 15 across, a little larger, for the heavier stuff |
| 8 | dynamite | none of its own: the blast is the level's `explosebig` or `explosesmall` (17.2) |
| 9 | the fire rocket, the meteor, the thunderbolt | `explosemeteor`, the ring on the floor: none of them is a brick the ball breaks, so only the weapons ask for it |

A destroyed object has its flash where its centre of mass was (`BrickDestroyed`). The first version's flash for every brick (7 to 15 across over 0.4 s, and 12 sparks) is gone with its sparks: no file gives a brick sparks, and the shark's frames show none.

### 19.6 Where the game uses them

| effect | what | from |
|---|---|---|
| a brick that breaks | the table above | `BrickDestroyed` (`PlayScene`) |
| dynamite | the explosion its level names (`target`: `explosebig` 25 across for the radius 5, `explosesmall` 15 for 3), at the charge's centre; a level that names none gets the one its radius fits | `DynamiteBlast` (`ObjectEffects`) |
| a cannonball bursts | `exploseyadro`, 10 across, 0.5 s [D] | `ShotHit` |
| the muzzle | a short flare (the first version's, not in a file) and `Smoke` along the barrel for 0.15 s [I] | `CannonFired` |
| a firework's rocket | `Trail/TrailOrange` from its tail while it flies (the variant with a scatter and a flame's colours; `TrailBlue` is for the bat's rocket [I]); the burst is one of the six of `Fireworks`, chosen by where and when it goes off | `objects().rockets()`, `RocketBurst` |
| a flicker | the two files its level names (`sparkKoster` and `smoke` for the cauldron, `spark` and `smoke` for the torch) attached to the top of its box, so that the fire is at the top of the post and over the logs, and they go when it is destroyed | `objects().flickers()`, the level's `names` |
| a tree that is hit | `LeafFall` from the crown, 3 above the trunk's centre of mass (the trunk is a capsule 10 tall, the foliage round its top [I]); every hit sheds 15 leaves, the last one too | `BrickHit` on a `Tree` |

Not used, because nothing in the data places them: `WaterBubbles` (the sea levels are inferred, no object or level names it; its type is used by the sparks), `Tornado` (the Wind prize is inferred, and the footage shows no tornado: section 15.3), `Flash` (size 30, 0.2 s; for the Thunderbolt's strike, if the weapons want it), `TrailBlue`, `TrailMeteor`, `Laser`, `Lightning`, `explosemeteor` (the weapons'; `mb_particle_smoke` runs every definition through the system all the same).

**Left as it was**, because the data does not define it: the prizes' halos and the Yo-Yo's string (16, which already read `Oreol` and `rubber`), the Explosive Ball's blast, the missed prize's sparkle, the teleports' ring and glow, the flicker's wavering light, the muzzle's flare, the shadows.

### 19.7 The numbers

Constants of `ParticleSystem.cpp` and `ObjectEffects.cpp`; the files' numbers are not repeated.

| constant | value | tag |
|---|---|---|
| default fade of a definition with none; shortest life of a particle | 0.5 s; 0.01 s | I |
| flat flash lift | 0.1 | I (the prizes' marks) |
| firework: slowest spark's share of `radius`, droop, shrink | 0.5, 5 units/s^2, 0.5 | I |
| trail: shrink over its life | 0.6 | I |
| smoke: slowest share of `speed`, smallest share of `size`, opacity | 0.7, 0.4, 0.4 | I |
| water bubble: life, sway, sway rate | 1.5 s, 0.15, 0.8 turns/s | I |
| leaf: fall speed range, floor, flutter rate | 0.7 to 1.3 x `speed`, 0.15, 1.5 turns/s | I |
| tornado: opacity | 0.35 | I |
| bolt: shakes a second | 30 | I |
| muzzle smoke, flare | 0.15 s, 0.5 s | I |
| tree crown above the trunk's centre | 3 | I |

### 19.8 Tested, and looked at

`mb_render_test` (the batches: blending, colour only, depth, order, whole triangles, thousands of quads), `mb_particle_tests` (the parser and what it reports; each type's behaviour, beams and bolts, the system's handles, the batches, the table of explosions; made-up files with the real numbers), `mb_particle_smoke` (the 20 real files read with nothing unknown, what OBJECTS.md says of them, every texture but the Tornado's, the halos' numbers `PrizeScene` uses, every file the objects and levels name, every definition run to its end), `mb_effects_smoke` (a flicker's fire, blasts, each kind of brick, leaves, the night, a rocket's trail and burst, on the GPU) and the cues of `--effect`. To look at a file without waiting for the game to need it: `MuggleBall3 --autoplay --level piratos-ostrovok --effect Fireworks/magenta@1,0,6,4 --screenshot out.png --simulate 1.25` (`name[@seconds[,x,y,z[,x2,y2,z2]]]`: the second point is a beam's other end).

The cost, on the dynamite chain of `knight-fruktovie-derevia` 0.2 s after the first charge (`--catch-prize bigball@0.5 --simulate 52.7`, then 15 steps): before, 627 draw items for 5 blasts with their sparks, 0.18 ms to update and 1.16 ms to draw and read back a 1024x768 frame; now 314 items and a few batches (with the cauldron's fire as well), 0.17 ms and 1.0 ms. A windowed run with a Tornado of 1,500 particles, smoke, a trail and a burst at once holds the display's 120 frames a second with the Metal validation layer on and nothing reported.

### 19.9 Still open

- **[?]** The flashes' opacity curve (a straight fade) and whether they are alpha blended in the original: the frames say alpha for the dynamite's fog, but one frame is not the whole.
- **[?]** The trail's colours: pale blue to orange as the particle ages, or the other way; the footage's rocket has an orange flame at its tail that the mesh may carry.
- **[?]** Whether `exploseball` and `explosesmall` are what the original gives bricks of type 3 and 6; the footage shows a small orange flash for both.
- **[?]** What `radius_offset` of the Oreol is, and the pulse (`speed`) of the halos; `PrizeScene`'s 4 units stand.
- **[?]** `Tornado`: how the original draws 1,500 particles of a missing texture over a column 160 tall, and what uses it.


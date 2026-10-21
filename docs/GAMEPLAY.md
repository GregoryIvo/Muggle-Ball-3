# Gameplay of the original, from footage

Research notes for step 4 (bat, ball, bricks, score, lives, level end) and step 5 (prizes).
Tags on claims: **[M]** measured from video, **[D]** read from the game's data files, **[I]** inferred, **[?]** not verified.
Times are video seconds (`currentTime`), frame step 1/30 s. Saved frames and numbers are in `reference/gameplay/` (index at the end).

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
3. **Effect.** The effect is a short orange flash with particles and the material's "dead" sound (shark, V1 54.4-54.5). `exploseType` selects it: 3, 5, 6, 8 (dynamite), 9 (fire rocket). Exact looks per type were not catalogued [?]. `explosePower` is 6.789 for live/people/prize objects and 1.0 for the rest; it is probably a blast impulse [I].
4. **Damage textures [D + M].** Materials with `brickType` 4 list one texture per energy level, worst first: `Box.jpg` (E4: Box-1D, -1C, -1B, -1A), `Skala.jpg` rock (E4: Skala--3 to --0), `Ground-2*`/`trava*` (E3: Ground-2-2, -1, -0). The last file is the intact look (see `data-damage-stage-textures-Box_Skala_Ground.png`). In footage black crack decals appear on ground and rock blocks as they are hit: V2 478-542 (a green tile with a black crack) and V2 566-598 (rock and ground blocks darkening). Which collisions count as hits for E > 1 was not isolated [?].
5. **Hit counting.** Bricks have `brickEnergy` hits; E3 bricks like the pirates and prizes were not shown being hit repeatedly [?].
6. **Score.** The score added is exactly the material's `brickScore` [M]: shark +91, island +66, then +132 at 55.4-55.5 and +360 at 55.7-55.8 (compound objects). **No combo or multiplier was seen** in about 10 events.
7. **Knocked-over but intact objects.** They are not scored; only destruction scores. Planks and sails stay afloat until hit [I]. What removes drifting pieces that nobody hits (e.g. V3 36-52) was not determined [?].
8. **No prize actors in levels [D].** Across all 83 levels there are no `prize` actors. Prizes are produced in play: they fall from the position of a destroyed brick (V1 66.5-69.5, x constant), and bullets from Thunderbolt and Meteorite Rain carry `disablePrize` [D].
9. **Level special objects [D].** There are 452 `metall`, 128 `tree`, 116 `dynamite`, 63 `firework`, 26 `generatorbad`, 17 `generatorgood`, 21 `cannon`, 21 `spring`, 13 `volcano`, 22 `teleport` objects. Dynamite explosion radii are 5 (big) and 3 (small). Fire rockets fly up at 15 u/s for 2.5 s. They are for later steps.
10. **Level end.** The level ends when the **last destroyable brick is gone**. In V1 level 1 the last pirate was killed by a lightning bolt at 144.1. The board looked empty (anchors and the sun are scenery). A dark fade began at about 144.4 and level 2 was visible at 145.5 (V1 142.6-145.0, 140.5-148.5). Prizes caught during the fade still show their caption (Increase Bat Size at 144.7). In V2 level 1 ended the same way at about 89 (V2 86.5-108). Whether type 4 pieces (ground, animals) must also be destroyed is not known [?].
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
- A glowing orb with a halo (blue aura = good, red = bad). A bunch of balloons was seen for Next Level (V1 143.5).
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
| endlevel | Next Level | blue | 120 | none | level ends at once: fade from about 254.5, next level at 255.2, no bonus (V2 254.3-255.2; V1 11077) |

Ammo prizes stack with other icons (up to 3 seen at once, V1 84-92). A second prize of the same kind was not watched [?].

## 8. Camera

- **Fixed [M].** In all three recordings the camera never moved: the rim stays registered to within 1 px with the bat at far left, right and centre, and during Earthquake (V2 179.8-184; the only change was ±3 px vertically for 2 frames at 180.2-180.3, which may be a video artefact). No camera shake was seen on explosions.
- **SmartCamera [?].** The Options menu has a "Smart Camera" switch ("SmartCamera" string). Its effect was never visible, so either these players had it off or it does nothing visible here.

## 9. Other surprises and open questions

- **Prizes are frequent.** About one caught prize per 6 s in V2 (about 120 captions in 1230 s), and V1 level 1 had roughly 10 in 90 s. Without numbers on destroyed bricks I cannot give a drop rate [?].
- **Players seldom lose a ball.** Over 3 hours V1 lost 1 ball plus 1 skull; V2 lost none. That says the game is forgiving or the players are good; no inference on difficulty.
- **A wall rope** keeps the ball in play for 30 s (V2 688-717.8); I did not capture the ball touching it [?].
- **Level timing.** Level 1 takes about 90 s in V1 and about 85 s in V2 for good players.
- **Not captured:** Yo-Yo, Magnet behaviour, Power/Big/Weak ball size effects, Drunken path, Wind mechanics, game over, edge-of-bat hits, hits-to-destroy for E3 bricks (pirates, prizes), far-wall at both ends.

## 10. Constants for step 4 (summary)

| constant | value |
|---|---|
| ball radius / speed | 0.6 / **15.0** u/s, y fixed at 0.6, speed renormalised after every collision |
| walls | side inner faces ±20.4 (centre limit ±19.8), far wall 22.6 (centre 22.0), mirror reflection |
| ball lost | centre z < -23.5 |
| bat | x only; collision sphere r 3, centre z about -21; centre x clamp about ±16 [?]; no speed cap |
| exit after bat hit | direction = normalize(ball - bat centre) in XZ, speed 15 (R 3.6-4.5 fits; ignore incoming angle) |
| held ball | z = bat z + 3.7 (about -17.3), x = bat x - 0.75; follows the bat |
| launch | click; velocity (-3.0, 14.7), i.e. 11.5 degrees left of +z; no auto-launch |
| lives | 2 hearts = spare lives; a heart is spent when the new bat appears; Life +1; no cap seen |
| loss to ready | about 2.5 s: ball removed, bat leaves about 0.5-0.8 s later, heart -1 at about 1.9 s, new bat swoops in over about 0.35 s, ball on bat 0.2-0.4 s later |
| brick | energy hits; damage texture index = energy-1 for multi-texture materials; explode `exploseDelay` ms after the last hit; score +brickScore at the explosion |
| level end | last brick gone: fade about 1 s, next level fade-in; no tally; `endlevel` prize does the same at once |
| prize | falls straight toward the bat at about 9.1 u/s; catch = +brickScore of the prize material; caption about 2.2 s |

## 11. Evidence index (`reference/gameplay/`)

- Camera overlays: `chillcalex-...-t54.2-camera-board-overlay.jpg`, `nocommentary-...-t75-camera-board-overlay-4x3-level1.jpg`, `...-t262-...-level4.jpg`.
- Launch and bat: `...-t54.0-ball-closeup.jpg`, `...-t137.85-bat-closeup-ball-contact.jpg`, `nocommentary-...-t6-normal-bat-closeup.jpg`, `...-t174-210-bat-width-after-increase-bat-size.jpg`.
- Bricks and score: `...-t53.5-56.9-score-timeline-hud.jpg`, `...-t54.1-54.5-shark-hit-and-explosion.jpg`, `...-t55.0-55.8-island-hit-and-sinking.jpg`, `data-damage-stage-textures-Box_Skala_Ground.png`, `nocommentary-...-t478-542-...cracked...jpg`, `...-t566-598-crack-damage-decals...jpg`.
- Lives: `...-t7873-7878-ball-lost-bat-respawn-overview.jpg`, `...-t7874.3-7878.0-lost-ball-respawn-hearts-5-to-4.jpg`, `...-t7876.3-7879.4-new-bat-flies-in-ball-on-bat.jpg`, `...-t8535.5-8539.5-skull-bat-killer-life-lost.jpg`.
- Level flow: `...-t52-level1-fade-in-bat-flies-in.jpg`, `...-t145.5-149.5-level2-start-ball-waits-on-bat.jpg`, `...-t142.6-145.0-level-end-fade.jpg`, `...-t11060-11110-game-ending-after-level83.jpg`.
- Prizes and HUD: the `...prize-captions...`, `...hud-*...`, `...magnet-hud-timer-30s.jpg`, `...laser-prize-score-plus360.jpg`, `...bonus-score-plus777.jpg`, `...earthquake-no-camera-shake.jpg`, `...wind-prize.jpg` frames.
- Numbers: `measurements-ball-bat-tracking.txt` (speeds, launches, hit table, wall and far-wall points, lost-ball point, prize fall).
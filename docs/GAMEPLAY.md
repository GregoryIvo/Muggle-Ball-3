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
3. **Effect.** The effect is a short orange flash with particles and the material's "dead" sound (shark, V1 54.4-54.5). `exploseType` selects it: 3, 5, 6, 8 (dynamite), 9 (fire rocket). Exact looks per type were not catalogued [?]. `explosePower` is 6.789 for live/people/prize objects and 1.0 for the rest; it is probably a blast impulse [I].
4. **Damage textures [D + M].** Materials with `brickType` 4 list one texture per energy level, worst first: `Box.jpg` (E4: Box-1D, -1C, -1B, -1A), `Skala.jpg` rock (E4: Skala--3 to --0), `Ground-2*`/`trava*` (E3: Ground-2-2, -1, -0). The last file is the intact look (see `data-damage-stage-textures-Box_Skala_Ground.png`). In footage black crack decals appear on ground and rock blocks as they are hit: V2 478-542 (a green tile with a black crack) and V2 566-598 (rock and ground blocks darkening). Which collisions count as hits for E > 1 was not isolated [?].
5. **Hit counting.** Bricks have `brickEnergy` hits; E3 bricks like the pirates and prizes were not shown being hit repeatedly [?].
6. **Score.** The score added is exactly the material's `brickScore` [M]: shark +91, island +66, then +132 at 55.4-55.5 and +360 at 55.7-55.8 (compound objects). **No combo or multiplier was seen** in about 10 events.
7. **Knocked-over but intact objects.** They are not scored; only destruction scores. Planks and sails stay afloat until hit [I]. What removes drifting pieces that nobody hits (e.g. V3 36-52) was not determined; the second study found that nothing does: weapons and blasts destroy loose pieces, falling and sinking do not ("Rules settled by the second study", 4).
8. **No prize actors in levels [D].** Across all 83 levels there are no `prize` actors. Prizes are produced in play: they fall from the position of a destroyed brick (V1 66.5-69.5, x constant), and bullets from Thunderbolt and Meteorite Rain carry `disablePrize` [D].
9. **Level special objects [D].** There are 452 `metall`, 128 `tree`, 116 `dynamite`, 63 `firework`, 26 `generatorbad`, 17 `generatorgood`, 21 `cannon`, 21 `spring`, 13 `volcano`, 22 `teleport` objects. Dynamite explosion radii are 5 (big) and 3 (small). Fire rockets fly up at 15 u/s for 2.5 s. They are for later steps.
10. **Level end.** The level ends when the **last destroyable brick is gone**. In V1 level 1 the last pirate was killed by a lightning bolt at 144.1. The board looked empty (anchors and the sun are scenery). A dark fade began at about 144.4 and level 2 was visible at 145.5 (V1 142.6-145.0, 140.5-148.5). Prizes caught during the fade still show their caption (Increase Bat Size at 144.7). In V2 level 1 ended the same way at about 89 (V2 86.5-108). Whether type 4 pieces (ground, animals) must also be destroyed was not known; the second study settled what is required, and the Next Level star as the usual end ("Rules settled by the second study", 2 and 3).
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

A second pass over V1 (all 83 levels: the HUD score read as text every 1/30 s, a caption detector, a fade detector; 1,355 caught prizes and about 10,600 score events; the per-level table is `rules-V1-per-level-score-event-summary.txt`) answered what sections 5.7, 5.10 and 6 left open. A third, independent pass looked again at five endings frame by frame (V1 levels 1, 2, 17, 23 and 30, and V2 level 1) and at 21 level starts. **Settled** below is what the passes agree on and what the game now does; **disputed** is where the re-check disagrees with the second pass, kept as [?] with its evidence. The frames are in `reference/gameplay/`: `rules-V1-*` and `rules-V2-*` are the second pass, `rules-chk-*` the re-check (names are shortened here to the part that tells them apart).

### Settled

1. **Level order is the order of `levels.xml`, all 83 [M].** The HUD shows 1 to 83 once each and in order, the board at the start of each run is the `levels.xml` entry of that number, the re-check matched 21 levels across the four stages, and the stage borders agree to the second (village 36:23, cowboys 1:40:26, knights 2:09:38). The "LEVEL : 62" saloon of `original-saloon.jpg` is `cowboys-napadenie`, which V1 shows as LEVEL : 51 (about 274,000 points there; the screenshot's 768,915 are more than V1 has at level 83): it comes from another run, which is not known [?]. Nothing changes: the game plays `Campaign`'s order. Evidence: `rules-V1-hud-level-number-at-start-of-each-run-1to83`, `rules-V1-hud-level-scan-*`, `rules-V1-*boards-at-level-start*`, `rules-V1-t6422-cowboys-napadenie-is-LEVEL51-...`, `rules-chk-V1-order-*`.
2. **The Next Level star ends a level at once, with whatever is left [M].** The orb is a golden sheriff star with a halo (`Prizes/EndLevel.x`, material `endlevel`), not the balloons of Increase Bat Size. The catch scores +120, shows the caption "Next Level" and starts the fade within 0.1 s, with pirates, gift boxes and a spar still on the board (V1 level 2 at 209.70; level 18; V2 level 1 at 88.3). In V1 it ended **70 of the 83 levels**; 13 ended naturally (levels 1, 10, 17, 19, 23, 30, 31, 34, 45, 57, 58, 64, 65). The fade lasts 0.9 to 1.25 s, to about 35 % of the luminance, and the next level is cut in at full brightness (V1 145.17 to 145.29; V2 88.35 to 89.65); a prize that is already falling is still caught during it. That the star is a common random drop is not shown: in every ending sampled at least 90 % of the board was gone already, which a chance that grows as the bricks run out would look like too [?]. Evidence: `rules-V1-t207.4-209.15-L2-...`, `rules-V1-t209.15-L2-zoom-...`, `rules-chk-V1-t209.4-209.7-L2-...`, `rules-chk-V1-t2008-2017.3-L18-...`, `rules-V1-t10037.9-10039.6-L76-...`, `rules-V1-end-of-level-frames-L1-to-L32...` (8 sheets), `rules-V1-end-sequences-*`, `rules-chk-V2-t85.5-88.4-L1-...`.
3. **Metall, teleports and the cannon are not required [M].** At natural ends objects of these types stay on the board: four teleport pyramids and spiked Metall fence pieces when the fade starts in level 30, the whole wall of 83 dynamic Metall pieces in level 31, an `int_cannon` in level 10. Metall can be destroyed (E1, 50 points): in level 30 a dynamite chain takes a fence in one frame (+1917, 1,419 to 3,336) and single +50 events happen, by ball or by blast is not separated; most of it survives a level. Dynamite, fireworks, generators, springs, volcanoes, cloth and vibro were never seen at a natural end (sticks, gift boxes and a firework stood at star ends, which proves nothing). Decision: they are **not required**, since none of them is a brick; a ball can still destroy them for their points, and a level that will not end with one of them standing would show the decision wrong. Evidence: `rules-V1-t3581.9-L30-...`, `rules-chk-V1-t3581.7-L30-zoom-teleports-and-metall-fence-remain-...`, `rules-V1-t3462-3467-L30-...`, `rules-V1-t3467.5-3470-L30-...`, `rules-V1-t3788.6-3790.7-L31-...`, `rules-V1-t1120.1-L10-...`, `rules-V1-t2368.0-L21-...`.
4. **Loose pieces are destroyed by weapons and blasts, not by falling or sinking [M].** The Laser beam (a straight beam up from the bat) kills planks with the ball across the board (V1 59.25, 90.2 to 91.2) and a pirate (83.08); a dynamite chain takes a fence in one frame; no death from falling, sinking, drifting or a timer was found, and big ground blocks drift and tilt for 100 s without dying (V2 level 5). So in the game a body that falls off the board stops being a target and is taken out, **scores nothing** (`GameplayRules::fallScores` is off) and has no explosion. Whether the lightning bolts seen at most level ends are the Thunderbolt prize is disputed, below. Evidence: `rules-V1-t58.6-59.3-L1-...`, `rules-V1-t59.2-59.37-L1-...`, `rules-V1-t90.2-91.2-L1-...`, `rules-V1-t82.7-83.14-L1-...`.
5. **Hits per brick are still not measured [?].** A pass at about 2 units does nothing (V1 131.4 to 132.0), a pirate wedged between two others for 0.3 s was not hurt, and the damage textures say energy = stages for type 4; the number of ball contacts that kill an E2 to E4 brick, and whether a weapon does more than 1, are open. The game keeps 1 damage per contact. Evidence: `rules-V1-t131.4-131.75-L1-...`, `rules-V1-t85.7-86.1-L1-...`.
6. **Prizes, for step 5 (recorded, nothing built) [M].**
   - About **14.7 % of destroyed objects** leave a prize that is caught (1,355 catches, about 16 a level; 13 to 14 % for the cheap E1 bricks, 4 to 6 % for objects worth 120 or more points, people and animals). Uncaught ones are not seen, so these are lower bounds. It is random, not every N-th: the gap between catches runs from 0 to 25 kills.
   - **Red prizes are ordinary drops from level 10 on.** No red-only value (+280 Increase Ball Speed, +220 Drunken, +240 Weak Ball, +666 Skull) occurs in levels 1 to 9 (about 100 catches); Increase Ball Speed starts at level 10 (94 events in levels 10 to 83, about 1.3 a level), Drunken at 23, Skull at 30 and 68, Weak Ball at 34; they are 9 % of the catches in levels 10 to 83. They do not come from the `generatorbad`s (levels 10 to 83 with and without one give 1.31 and 1.26 Increase Ball Speed a level). The generators were never seen to produce anything, and their gift boxes stood through whole levels (2, 21, 22, 76). A hard unlock level is not proven.
   - Weapon kills carry `disablePrize` [D] for the Thunderbolt and the meteors.
   - Evidence: `rules-V1-prize-captions-at-score-plus-280-events-increase-ball-speed`, `rules-V1-L2-generator-watch-frames-t148-186`, and the per-level table.

### What the game does with it (step 4)

| rule | in the game |
|---|---|
| required to end a level | `TargetRule::DynamicBricks`, the default: the dynamic `brick`s of material type 0 and 4 and the dynamic `tree`s. Not required: kinematic bricks (floor tiles, sharks, birds), metall, teleports, cannons, and the rest of item 3. `BrickType` (H-type) is refuted and kept for comparing and for tests |
| safety net | the look at what the ball can reach and the stall rule stay (section 12 says how often they fire) |
| natural end | `LevelComplete` as the last required object explodes; the picture holds for **0.45 s**, fades out in 0.8 s, and the next level comes up out of the dark |
| a piece off the board | not a target any more, taken out (`ActorFell`), no points, no explosion |
| level order | `levels.xml`, as `Campaign` has it |
| Next Level, prizes | not built (step 5); `GameSession::forceLevelComplete()` is what the star will call |

### Disputed by the re-check [?]

1. **The hold is not a fixed 0.45 s.** The re-check puts the fade 0.05 to 0.1 s after the last counted object has *left the board*. A plain brick, block or crate leaves at its score, a person or an animal 0.35 to 0.4 s after it (the body shrinks away first), and 0.45 s after the score, the second pass's figure, is the average of the two. Level 23: the last block hit at 2629.57, gone at 2629.60, fade from 2629.65. Level 30: the last crate +57 at 3581.70 to 3581.77, fade from 3581.83. Level 1: `pirat1` paid (+290) at 143.75, the body gone at 144.15, fade from 144.12. Level 17: the gorilla paid (+190) at 1943.6, shrinking to 1944.0, fade from 1943.97. The game has no shrinking bodies (a brick vanishes with a flash as it explodes), so it holds 0.45 s: right for a creature, about 0.3 s long for a brick, and the 0.4 s flash plays out inside it. Where the animations come (step 5) the fade should start about 0.07 s after the last removal ends. Levels 10, 19 and 31 end 1.1 to 2.0 s after their last readable score (in level 31 five lightning strikes hit the Metall wall without a score meanwhile), levels 34, 57 and 65 0 to 0.3 s after it: not explained.
2. **That kinematic bricks are not required rests on one ending, which fits either reading.** The second pass saw level 23 end with a kinematic `Ground-2-ground` block alive. The re-check's 30 fps frames show that block, the last object on the board (the player had destroyed 83 of the 84), hit at 2629.57 and gone at 2629.60, the fade 0.05 s later: as the 0.45 s after the +325 at 2629.18 predicts, and as "the last brick, kinematic or not, is gone" does. At the other natural ends the board had no kinematic brick left (level 65: all 84 ground blocks and 12 water bricks gone; level 17: 72). So the footage does not show that kinematic bricks can stay, and "every brick-class object counts, moving or not; metall, teleports and cannons do not" is as good a reading and the simpler one. V2 level 1 may favour it: the last pirate dies at 86.2 and the board looks empty, but no fade follows until the star is caught at 88.3 (+120, caption from 88.55, next level at 89.65), where a 0.45 s rule would have ended it at 86.7; a counted object may have been out of sight (the kinematic shark, whose fin is seen at 14 s and not after about 50 s?) [?]. The game follows the second pass (dynamic only). The other reading is one condition in `isTarget` (take `dynamic &&` out), and it costs the autopilot nothing it could measure: with it the required objects of the 83 levels grow from 9,840 to 12,444, and the same 63 levels finish in 900 s (47 by destroying everything, 16 with the stall rule), the same 20 do not. Evidence: `rules-chk-V1-t2629.40-2629.67-L23-...`, `rules-chk-V1-t2629.5-2630.45-L23-...`, `rules-chk-V1-t2629.0-2629.8-L23-...`, `rules-chk-V1-t7846-8035-L65-...`, `rules-chk-V2-t85.5-88.4-L1-...`, `rules-chk-V2-t87.2-L1-...`, `rules-chk-V2-t8-14-L1-...`, `rules-chk-V2-t44-74-L1-...`.
3. **The lightning bolts are not shown to be the Thunderbolt prize.** V1 level 1 has no Thunderbolt caption from 54 to 144.8 (scanned at 2 s steps or finer), yet bolts strike at 133.0, 139.2, 142.0 and 143.9, all in its last 11 s; V2 level 1 has none from 0 to 90 and a bolt about every 2 s from 81.0 to 89.0; level 30 none in the 30 s before the bolt at 3581.77. In V1 level 2 one caption (199.5) is followed by bolts at 200 and 206.5, so one catch may give several strikes. The score comes first and the bolt is drawn 1 to 2 frames later (+34 at 132.90, bolt first visible at 132.94); frame differencing finds no plank vanishing at that +34, and the visible strike at 133.0 to 133.4 scored nothing. Who throws the bolts is unknown, possibly a help of the game when few bricks are left [?]; in V2 at 81.0 to 81.5 a prize orb appears exactly where a bolt struck, which does not fit `disablePrize` for bolt kills. The Laser beam, dynamite chains and the absence of any kill by falling stand. Evidence: `rules-chk-V1-t132.84-132.97-L1-...`, `rules-chk-V1-t132.2-133.2-L1-...`, `rules-chk-V1-t139.2-142.3-L1-...`, `rules-chk-V1-t54-100-L1-caption-scan-...`, `rules-chk-V1-t186-209-L2-...`, `rules-chk-V1-t3550-3582-L30-...`.

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
| brick | energy hits; damage texture index = energy-1 for multi-texture materials; explode `exploseDelay` ms after the last hit; score +brickScore at the explosion |
| level order | exactly the order of `levels.xml`, all 83 [M] ("Rules settled by the second study", 1) |
| required to end a level | the dynamic `brick`s of material type 0 and 4 and the dynamic `tree`s (H-dyn); not metall, teleports, cannons, and, by decision, not dynamite, fireworks, generators, springs, volcanoes, cloth, vibros; kinematic bricks not required is disputed [?]. H-type (type 0 only) is refuted |
| level end | the last required object gone: the picture holds about 0.45 s (the re-check finds 0.1 s after a brick and 0.4 s after a creature, whose body shrinks away [?]), fades out in about 1 s (to 35 % of the luminance, then a hard cut), the next level follows; no tally. The `endlevel` star (+120, "Next Level") does it within 0.1 s, with the board not cleared, and ended 70 of 83 levels |
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
- The second study (`rules-V1-*`, `rules-V2-*`) and its re-check (`rules-chk-*`), by the item of "Rules settled by the second study" they belong to:
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
- When the last target is gone, or the game is lost, the picture holds for 0.45 s ("Rules settled by the second study"; it was 0.3 s), fades to
  black in 0.8 s with the HUD left in sight, and the next level (or the end of the game) follows at once: no tally and no Loading form
  (V1 144.4-145.5) [M for the hold, I for the fade: the footage fades to 35 % of the luminance in about a second and cuts]. Neither the
  fade nor the pause can be interrupted: Escape does nothing while a level is fading out, because leaving from the pause would lose how it ended
  (a win never recorded, a game over that never reaches the name entry), and the click or key that ends a pause is not given to the level, or it
  would launch the ball.
- The ball held on the bat goes on with the bat between the session's steps, as the bat does, so that it does not shimmer against it at 120 Hz.

**Bricks and the ball.**
- A brick that is hit shows the texture of its damage state (`damageTextureIndex`): a crate (Box.jpg) goes from whole to cracked, smashed and
  black as its energy falls from 4 to 1 [D]; one that explodes is gone at that moment, with an orange flash (`Oreols/02.png`, 0.4 s, swelling and
  fading) and twelve sparks thrown up and out [I: the footage's flash is orange and short, V1 54.40; which particle file an `exploseType` would
  pick is not known, so every brick has this one].
- The ball rolls, turned about the horizontal axis across its path by the distance over its radius, and has a soft blob of shadow on the floor
  [I; the footage has a "soft shadow" under it, section 2].
- The sounds are in `AUDIO.md`; `ballHitBat`, `bat_dead` and `endlevel` are names in `sounds.xml` that no material uses, so which event plays them is
  inferred.

**A level that cannot be finished.** The level ends with the required objects of the second study (the dynamic bricks of type 0 and 4 and the
trees); a required object that nothing can touch would make it impossible. Two things are done about it, both in `GameSession`
(`SessionOptions::ignoreUnreachable`, `GameplayRules::stallSeconds`); neither is in the original as far as the footage shows, and the footage cannot
tell whether it had a help of its own (the lightning bolts, "Disputed", 3):
- A target the ball cannot get at is not one (`BallReach`): the plane the ball moves on is flood-filled from the bat's end of the board around the
  walls, platforms and pinned buildings, a ball's radius from each; a target at the ball's height with no such ground near it (beyond a wall or the
  board's edge, in a slot narrower than the ball) is out, and so is one above the ball that is pinned, on a path, has no gravity or has no such
  ground under it (on a platform or a wall top). The look is made at the start and twice a second for the loose ones. In the real levels it finds 136
  targets in 39 of the 83 (15 in knight-castle, 10 in knight-nazad-k-mame, 8 in knight-river, the others by ones and twos).
- When balls have been in flight for 90 s without touching a brick and at most 12 targets (or a quarter of those the level began with) are left,
  they are let go. This is for what the look cannot see: a mast held in the air by joints with nothing left to hold them, a cactus that settled
  10 cm above the ball's reach, a bird that hangs where the physics left it [?]. It is generous (a player could stall a level out), but only with
  few targets left and after a minute and a half.

How many levels the autopilot finishes in 900 s of game time (`mb_gameplay_smoke --seconds 900`, which ends the levels as the game does; it
launches at once and keeps the bat under the ball with no aim, with 99 hearts so that it plays on, and aiming at the nearest brick or among the
four nearest cleared about the same number): **63 of 83**, the median level in 7 minutes. 47 ended by destroying every target and
**16 by the stall rule**, which let go of 1 to 6 targets (35 in all, a single one in 9 of the 16). Only 6 of those 16 needed it to finish within the
time (the 10 others finish without it, 3 to 105 s later), and the look at what the ball can reach lets 2 more levels finish: without either safety net
(`--strict`) the autopilot finishes 55, and the 8 it loses stand still with 1 to 6 targets left that nothing touched for 90 s [?]. So
the stall rule fires in a quarter of the levels the bot finishes and decides the outcome in 6 of 83: still needed, and it is the aimless bot
that stalls; a person aims, and has the weapons. The 20 levels it does not finish (11 knights', 7 village's and 2 pirates') are slow, not stuck: each
was still destroying bricks in the last 30 s of the run (but village-tennis, with one target left and nothing destroyed for 80 s), and most have 100 to
250 targets. A person with the prizes will be faster.

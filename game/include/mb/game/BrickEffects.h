#pragma once

#include "mb/game/MaterialLibrary.h"

#include <span>
#include <string_view>

namespace mb {

// The particle effects (Data/Particles, named as ParticleLibrary::find takes them) that go off where an object breaks, by its
// material's `exploseType` (docs/GAMEPLAY.md section 19.5). The data does not link the type to a file, so the table is a choice [I]
// from what the files are called, what the footage shows, and the sizes (a small flash for light things, a larger one for the
// stuff of buildings and rocks):
//   3  prizes, bat bullets, the ships' hulls and sails, the shark:   exploseball (size 10)
//   5  people, animals, trees, the interactive objects:               none: they shrink away, and the footage has no flash on a pirate [M]
//   6  buildings, ground, crates, metal (the ones with splinters):   explosesmall (size 15)
//   8  dynamite:                                                      none here: the blast is the level's own (`explosebig`, `explosesmall`)
//   9  the fire rocket, the meteor, the thunderbolt:                  explosemeteor (a ring on the floor); none of them is a brick the
//                                                                     ball breaks, so only the weapons ask
// and none for 0 (the system's, the board, light, Default and the shuttlecock `volan`) and 2 (the ball, bullets, the board) and for a type
// the data does not have.
std::span<const std::string_view> explosionEffects(int exploseType);

// The effects of an object of this material: its type's, but for the island `Ostrov.jpg` (type 3), which the footage shows sinking with
// bubbles and no flash (chillcalex-...-t55.0-55.8-island-hit-and-sinking) [M, I]: none. None for no material.
std::span<const std::string_view> explosionEffects(const GameMaterial* material);

}  // namespace mb

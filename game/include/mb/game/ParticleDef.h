#pragma once

#include "mb/engine/Math.h"
#include "mb/engine/Renderer.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mb {

// The ten `type`s of the <particle> elements in Data/Particles/*.xml (docs/OBJECTS.md section 8 says what each is for and
// what its attributes mean; the meanings are inferred, the executable is not at hand).
enum class ParticleType : std::uint8_t {
    Oreol,        // a halo: a sprite that spins (the prizes', which PrizeScene draws; the particle system does not)
    Flash,        // one sprite that swells and fades: the explosions
    Firework,     // a burst of sparks flying out and slowing
    Laser,        // a beam between two points, from one colour to the other
    Lightning,    // a jagged bolt of segments between two points
    Trail,        // sprites left behind a moving thing, fading
    LeafFall,     // leaves that drift down
    Smoke,        // puffs that rise in a narrow cone
    WaterBubble,  // sprites that rise from a disc: the torch's sparks and the bubbles of the sea
    Tornado,      // a funnel of particles turning about a column
};
inline constexpr int kParticleTypeCount = 10;

std::string_view particleTypeName(ParticleType type);
std::optional<ParticleType> particleTypeByName(std::string_view name);  // the data's spelling, any case

// One <particle>, as the file has it: every attribute the 20 files use. An attribute a definition does not give is 0, empty
// or false. Colours are the file's ARGB (`0xffff0000`) as four floats; the alpha of the files that give one (0x00...) is
// never meant as "invisible", which is why drawing leaves it out (docs/OBJECTS.md section 8).
struct ParticleDef {
    std::uint32_t id = 0;  // its place in the library, for lookups by definition
    std::string file;      // the file it is in, without the extension ("Fireworks")
    std::string name;      // `name` ("red")
    ParticleType type = ParticleType::Flash;
    std::string texture;   // `texture`, below Textures ("Particles/ParticleBubble.png")

    int count = 0;         // `num`
    float size = 0;        // `size`
    float radius = 0;      // `radius`
    float radiusOffset = 0;  // `radius_offset`
    float radiusSmall = 0, radiusBig = 0;  // `radius_small`, `radius_big`
    float speed = 0;         // `speed`
    float speedAngular = 0;  // `speed_angular`
    float angle = 0;         // `angle`, degrees
    float height = 0, width = 0;  // `height`, `width`
    float fadeTime = 0;      // `fade_time_msec`, in seconds
    float shiftCoeff = 0, shakeCoeff = 0;  // `shift_coeff`, `shake_coeff`
    bool inverseRotation = false;  // `inverse_rotation`
    bool modulateColour = false;   // `modulate_colour`: the colour multiplies the texture's; no or absent: it replaces it
    std::optional<Vec3> direction, dimension;         // `direction`, `dimension`
    std::optional<Vec3> shadowPlane, shadowPosition;  // `shadow_plane`, `shadow_position`
    std::optional<Color> colour, colour1, colour2;    // `colour`, `colour1`, `colour2`
};

// The definitions of Data/Particles/*.json (converted from the XML by tools/objects.py), in file order. A definition is
// named by its file and its own name: "Fireworks/red"; the file alone ("explosebig") is its first definition. Lookups
// ignore case.
class ParticleLibrary {
public:
    struct Source {
        std::string stem;  // the file's name without the extension
        std::string json;
    };

    ParticleLibrary() = default;

    // Reads every *.json in `directory` (Data/Particles), in name order. What cannot be read is left out and said in problems(),
    // and logged; a missing folder gives an empty library.
    static ParticleLibrary load(const std::filesystem::path& directory);
    static ParticleLibrary parse(std::span<const Source> sources);

    const ParticleDef* find(std::string_view spec) const;
    // The definitions of one file, in order; empty if there is no such file.
    std::span<const ParticleDef> file(std::string_view stem) const;
    std::span<const ParticleDef> all() const { return defs_; }

    // "File/name: attribute" of each attribute no ParticleDef field holds (it is read and ignored), and what could not be read:
    // a file that is no JSON, a definition with no known `type`, an attribute of the wrong kind.
    const std::vector<std::string>& unknownAttributes() const { return unknown_; }
    const std::vector<std::string>& problems() const { return problems_; }

private:
    void add(const Source& source);

    std::vector<ParticleDef> defs_;
    std::unordered_map<std::string, std::size_t> byName_;                       // "file/name" and "file" -> index
    std::unordered_map<std::string, std::pair<std::size_t, std::size_t>> files_;  // "file" -> first, count
    std::vector<std::string> unknown_, problems_;
};

}  // namespace mb

#pragma once

// Compiles and links the five programs the GL back end draws with: the mesh shader (with and without the
// alpha-test discard, see engine/shaders/gl/mesh.frag), lines, sprites and effects. One instance per
// Renderer, built once at construction; uniform locations are cached here, not looked up per draw.
#include "GlFunctions.h"
#include "GlPlatform.h"

namespace mb::render::gl {

// Every uniform location any of the five programs has; a program that has no uniform of that name leaves it
// at -1, which glUniform*/glUniformMatrix4fv silently ignore (harmless to still call).
struct GlProgram {
    GLuint program = 0;
    GLint viewProj = -1;
    GLint world = -1;
    GLint normalMatrix = -1;
    GLint baseTexture = -1;
    GLint diffuse = -1;
    GLint ambient = -1;
    GLint emissive = -1;
    GLint lightDirection = -1;
    GLint lightDiffuse = -1;
    GLint lightAmbient = -1;
    GLint flags = -1;
    GLint pixelToClip = -1;
};

class ProgramCache {
public:
    // Throws std::runtime_error (with the shader or link info log) if a program fails to build.
    ProgramCache();
    ~ProgramCache();
    ProgramCache(const ProgramCache&) = delete;
    ProgramCache& operator=(const ProgramCache&) = delete;

    // `discardFree` picks the program compiled without the alpha-test branch (Renderer.cpp decides when:
    // opaqueAlpha && material.diffuse.a >= 1 && blend == Opaque).
    const GlProgram& mesh(bool discardFree) const { return discardFree ? meshNoDiscard_ : mesh_; }
    const GlProgram& line() const { return line_; }
    const GlProgram& sprite() const { return sprite_; }
    const GlProgram& effect() const { return effect_; }

private:
    GLuint meshVertex_ = 0, meshFragment_ = 0, meshFragmentNoDiscard_ = 0;
    GLuint lineVertex_ = 0, lineFragment_ = 0;
    GLuint spriteVertex_ = 0, spriteFragment_ = 0;
    GLuint effectVertex_ = 0, effectFragment_ = 0;
    GlProgram mesh_, meshNoDiscard_, line_, sprite_, effect_;

    void releaseShaders();
    void releasePrograms();
};

}  // namespace mb::render::gl

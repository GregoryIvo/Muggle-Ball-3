#include "ProgramCache.h"

#include "mb/engine/Log.h"

#include "gl_shaders.h"

#include <initializer_list>
#include <stdexcept>
#include <string>

namespace mb::render::gl {

namespace {

GLuint compile(GLenum stage, const char* name, const char* body) {
    const GLuint shader = glCreateShader(stage);
    const char* sources[2] = {Platform::preamble(stage == GL_FRAGMENT_SHADER), body};
    glShaderSource(shader, 2, sources, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char info[2048] = {};
        GLsizei length = 0;
        glGetShaderInfoLog(shader, sizeof(info) - 1, &length, info);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("compiling shader ") + name + " failed: " + info);
    }
    return shader;
}

// Binds attribute 0, 1, 2 (in that order) to the names given, matching engine/src/render/gl/GlPlatform.h's
// contract and the vertex layouts in RenderCommon.h and Vertex.h, then links and caches every uniform
// location the back end uses (a program without one of these leaves it at -1).
GlProgram link(GLuint vertex, GLuint fragment, std::initializer_list<const char*> attributes, const char* name) {
    GlProgram p;
    p.program = glCreateProgram();
    glAttachShader(p.program, vertex);
    glAttachShader(p.program, fragment);
    GLuint location = 0;
    for (const char* attribute : attributes) {
        glBindAttribLocation(p.program, location++, attribute);
    }
    glLinkProgram(p.program);
    GLint linked = GL_FALSE;
    glGetProgramiv(p.program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char info[2048] = {};
        GLsizei length = 0;
        glGetProgramInfoLog(p.program, sizeof(info) - 1, &length, info);
        const GLuint failed = p.program;
        glDeleteProgram(failed);
        throw std::runtime_error(std::string("linking program ") + name + " failed: " + info);
    }
    p.viewProj = glGetUniformLocation(p.program, "viewProj");
    p.world = glGetUniformLocation(p.program, "world");
    p.normalMatrix = glGetUniformLocation(p.program, "normalMatrix");
    p.baseTexture = glGetUniformLocation(p.program, "baseTexture");
    p.diffuse = glGetUniformLocation(p.program, "diffuse");
    p.ambient = glGetUniformLocation(p.program, "ambient");
    p.emissive = glGetUniformLocation(p.program, "emissive");
    p.lightDirection = glGetUniformLocation(p.program, "lightDirection");
    p.lightDiffuse = glGetUniformLocation(p.program, "lightDiffuse");
    p.lightAmbient = glGetUniformLocation(p.program, "lightAmbient");
    p.flags = glGetUniformLocation(p.program, "flags");
    p.pixelToClip = glGetUniformLocation(p.program, "pixelToClip");
    // The sampler always reads texture unit 0 (the back end never binds any other unit), so this is the
    // only place the uniform needs setting.
    if (p.baseTexture >= 0) {
        glUseProgram(p.program);
        glUniform1i(p.baseTexture, 0);
    }
    return p;
}

}  // namespace

ProgramCache::ProgramCache() {
    try {
        meshVertex_ = compile(GL_VERTEX_SHADER, "mesh.vert", shaders::gl_mesh_vert);
        meshFragment_ = compile(GL_FRAGMENT_SHADER, "mesh.frag", shaders::gl_mesh_frag);
        meshFragmentNoDiscard_ =
            compile(GL_FRAGMENT_SHADER, "mesh.frag (discard-free)", shaders::gl_mesh_frag_nodiscard);
        lineVertex_ = compile(GL_VERTEX_SHADER, "line.vert", shaders::gl_line_vert);
        lineFragment_ = compile(GL_FRAGMENT_SHADER, "line.frag", shaders::gl_line_frag);
        spriteVertex_ = compile(GL_VERTEX_SHADER, "sprite.vert", shaders::gl_sprite_vert);
        spriteFragment_ = compile(GL_FRAGMENT_SHADER, "sprite.frag", shaders::gl_sprite_frag);
        effectVertex_ = compile(GL_VERTEX_SHADER, "effect.vert", shaders::gl_effect_vert);
        effectFragment_ = compile(GL_FRAGMENT_SHADER, "effect.frag", shaders::gl_effect_frag);

        mesh_ = link(meshVertex_, meshFragment_, {"inPosition", "inNormal", "inUv"}, "mesh");
        meshNoDiscard_ =
            link(meshVertex_, meshFragmentNoDiscard_, {"inPosition", "inNormal", "inUv"}, "mesh (discard-free)");
        line_ = link(lineVertex_, lineFragment_, {"inPosition", "inColor"}, "line");
        sprite_ = link(spriteVertex_, spriteFragment_, {"inPosition", "inUv", "inColor"}, "sprite");
        effect_ = link(effectVertex_, effectFragment_, {"inPosition", "inUv", "inColor"}, "effect");
    } catch (...) {
        releaseShaders();
        throw;
    }
}

ProgramCache::~ProgramCache() {
    for (const GlProgram* p : {&mesh_, &meshNoDiscard_, &line_, &sprite_, &effect_}) {
        if (p->program) {
            glDeleteProgram(p->program);
        }
    }
    releaseShaders();
}

void ProgramCache::releaseShaders() {
    for (GLuint shader : {meshVertex_, meshFragment_, meshFragmentNoDiscard_, lineVertex_, lineFragment_,
                          spriteVertex_, spriteFragment_, effectVertex_, effectFragment_}) {
        if (shader) {
            glDeleteShader(shader);
        }
    }
    meshVertex_ = meshFragment_ = meshFragmentNoDiscard_ = 0;
    lineVertex_ = lineFragment_ = 0;
    spriteVertex_ = spriteFragment_ = 0;
    effectVertex_ = effectFragment_ = 0;
}

}  // namespace mb::render::gl

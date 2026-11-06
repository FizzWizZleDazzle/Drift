#pragma once

// Minimal OpenGL function table. Desktop loads GL 3.3 core through
// SDL_GL_GetProcAddress; the web build links WebGL2 (GLES 3.0) directly.

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#ifndef APIENTRY
#define APIENTRY
#endif
#else
#include <SDL3/SDL_opengl.h>
#endif

namespace drift::gfx::gl {

// X(return type, name without the gl prefix, parameter list)
#define DRIFT_GL_FUNCTIONS(X)                                                              \
    X(void, ActiveTexture, (GLenum texture))                                               \
    X(void, AttachShader, (GLuint program, GLuint shader))                                 \
    X(void, BindBuffer, (GLenum target, GLuint buffer))                                    \
    X(void, BindBufferBase, (GLenum target, GLuint index, GLuint buffer))                  \
    X(void, BindBufferRange, (GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)) \
    X(void, BindFramebuffer, (GLenum target, GLuint framebuffer))                          \
    X(void, BindTexture, (GLenum target, GLuint texture))                                  \
    X(void, BindVertexArray, (GLuint array))                                               \
    X(void, BlendEquationSeparate, (GLenum modeRGB, GLenum modeAlpha))                     \
    X(void, BlendFuncSeparate, (GLenum srcRGB, GLenum dstRGB, GLenum srcA, GLenum dstA))   \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))  \
    X(void, BufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data)) \
    X(GLenum, CheckFramebufferStatus, (GLenum target))                                     \
    X(void, Clear, (GLbitfield mask))                                                      \
    X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                      \
    X(void, CompileShader, (GLuint shader))                                                \
    X(GLuint, CreateProgram, (void))                                                       \
    X(GLuint, CreateShader, (GLenum type))                                                 \
    X(void, DeleteBuffers, (GLsizei n, const GLuint* buffers))                             \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint* framebuffers))                   \
    X(void, DeleteProgram, (GLuint program))                                               \
    X(void, DeleteShader, (GLuint shader))                                                 \
    X(void, DeleteTextures, (GLsizei n, const GLuint* textures))                           \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint* arrays))                         \
    X(void, Disable, (GLenum cap))                                                         \
    X(void, DrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices))  \
    X(void, Enable, (GLenum cap))                                                          \
    X(void, EnableVertexAttribArray, (GLuint index))                                       \
    X(void, FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(void, GenBuffers, (GLsizei n, GLuint* buffers))                                      \
    X(void, GenFramebuffers, (GLsizei n, GLuint* framebuffers))                            \
    X(void, GenTextures, (GLsizei n, GLuint* textures))                                    \
    X(void, GenVertexArrays, (GLsizei n, GLuint* arrays))                                  \
    X(GLenum, GetError, (void))                                                            \
    X(void, GetActiveUniformBlockName, (GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLchar* name)) \
    X(void, GetIntegerv, (GLenum pname, GLint* data))                                      \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* log)) \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint* params))                   \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* log)) \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint* params))                     \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar* name))                     \
    X(void, LinkProgram, (GLuint program))                                                 \
    X(void, PixelStorei, (GLenum pname, GLint param))                                      \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data)) \
    X(void, Scissor, (GLint x, GLint y, GLsizei w, GLsizei h))                             \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)) \
    X(void, TexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* pixels)) \
    X(void, TexParameteri, (GLenum target, GLenum pname, GLint param))                     \
    X(void, TexSubImage2D, (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* pixels)) \
    X(void, Uniform1i, (GLint location, GLint v0))                                         \
    X(void, UniformBlockBinding, (GLuint program, GLuint blockIndex, GLuint binding))      \
    X(void, UseProgram, (GLuint program))                                                  \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer)) \
    X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h))

struct Functions {
#define DRIFT_GL_MEMBER(ret, name, params) ret (APIENTRY* name) params = nullptr;
    DRIFT_GL_FUNCTIONS(DRIFT_GL_MEMBER)
#undef DRIFT_GL_MEMBER

    // Returns false and names the first missing function in *missing.
    bool load(const char** missing);
};

} // namespace drift::gfx::gl
